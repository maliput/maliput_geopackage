// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "maliput_geopackage/geopackage/geopackage_parser.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <maliput/math/vector.h>
#include <sqlite3.h>

namespace maliput_geopackage {
namespace geopackage {

namespace {

// RAII wrapper for sqlite3 database connection.
class SqliteDatabase {
 public:
  explicit SqliteDatabase(const std::string& db_path) {
    const int rc = sqlite3_open_v2(db_path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
      const std::string err_msg = sqlite3_errmsg(db_);
      sqlite3_close(db_);
      throw std::runtime_error("Failed to open GeoPackage at " + db_path + ": " + err_msg);
    }
  }

  ~SqliteDatabase() {
    if (db_) {
      sqlite3_close(db_);
    }
  }

  sqlite3* get() const { return db_; }

 private:
  sqlite3* db_{nullptr};
};

// RAII wrapper for sqlite3 statement.
class SqliteStatement {
 public:
  SqliteStatement(sqlite3* db, const std::string& query) {
    const int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare query '" + query + "': " + sqlite3_errmsg(db));
    }
  }

  ~SqliteStatement() {
    if (stmt_) {
      sqlite3_finalize(stmt_);
    }
  }

  bool Step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
      return true;
    } else if (rc == SQLITE_DONE) {
      return false;
    } else {
      throw std::runtime_error("Failed to step query: " + std::to_string(rc));
    }
  }

  std::string GetColumnText(int col) {
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    return text ? std::string(reinterpret_cast<const char*>(text)) : "";
  }

  const void* GetColumnBlob(int col) { return sqlite3_column_blob(stmt_, col); }

  int GetColumnBytes(int col) { return sqlite3_column_bytes(stmt_, col); }

 private:
  sqlite3_stmt* stmt_{nullptr};
};

std::vector<maliput::math::Vector3> ParseGeopackageGeometry(const void* data, int bytes) {
  if (data == nullptr ||
      bytes < 21) {  // Minimum: 8 (header) + 1 (byte order) + 4 (type) + 4 (num_points) + 4 (at least 1 point: 2*8)
    std::cerr << "ParseGeopackageGeometry: Invalid data size " << bytes << std::endl;
    return {};
  }

  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  const uint8_t* end = ptr + bytes;

  // Skip GeoPackage Header (8 bytes for no envelope, flags=0x01)
  // Magic (2), Version (1), Flags (1), SRS_ID (4)
  ptr += 8;
  if (ptr > end - 9) {  // Need at least 9 more bytes (1 + 4 + 4)
    std::cerr << "ParseGeopackageGeometry: Not enough data after header" << std::endl;
    return {};
  }

  // WKB Geometry
  // Byte order (1 byte)
  uint8_t byte_order = *ptr;
  ptr += 1;

  // We assume Little Endian (1) as per the python script generator.
  // A full implementation would handle Big Endian (0).
  if (byte_order != 1) {
    std::cerr << "ParseGeopackageGeometry: Unsupported byte order " << static_cast<int>(byte_order) << std::endl;
    return {};
  }

  // Type (4 bytes)
  if (ptr > end - 4) {
    std::cerr << "ParseGeopackageGeometry: Not enough data for type" << std::endl;
    return {};
  }
  uint32_t wkb_type;
  std::memcpy(&wkb_type, ptr, 4);
  ptr += 4;

  // Mask off type flags (Z, M, etc.) to get the base geometry type
  // Bit 0x80000000 indicates Z coordinate, 0x40000000 indicates M coordinate
  uint32_t base_type = wkb_type & 0x0FFFFFFF;

  // wkbLineString = 2
  if (base_type != 2) {
    std::cerr << "ParseGeopackageGeometry: Unsupported geometry type " << base_type << " (raw type: " << wkb_type << ")"
              << std::endl;
    return {};
  }

  // Num points (4 bytes)
  if (ptr > end - 4) {
    std::cerr << "ParseGeopackageGeometry: Not enough data for num_points" << std::endl;
    return {};
  }
  uint32_t num_points;
  std::memcpy(&num_points, ptr, 4);
  ptr += 4;

  // Validate we have enough data for all points
  if (num_points > 1000000) {  // Sanity check
    std::cerr << "ParseGeopackageGeometry: Unreasonable number of points " << num_points << std::endl;
    return {};
  }

  if (ptr > end - (static_cast<int>(num_points) * 16)) {
    std::cerr << "ParseGeopackageGeometry: Not enough data for " << num_points << " points" << std::endl;
    return {};
  }

  std::vector<maliput::math::Vector3> points;
  points.reserve(num_points);

  for (uint32_t i = 0; i < num_points; ++i) {
    double x, y;
    std::memcpy(&x, ptr, 8);
    ptr += 8;
    std::memcpy(&y, ptr, 8);
    ptr += 8;
    points.emplace_back(x, y, 0.0);
  }

  std::cerr << "ParseGeopackageGeometry: Successfully parsed " << num_points << " points" << std::endl;
  return points;
}

}  // namespace

GeoPackageParser::GeoPackageParser(const std::string& gpkg_file_path) {
  SqliteDatabase db(gpkg_file_path);

  // 1. Parse Junctions
  {
    SqliteStatement stmt(db.get(), "SELECT junction_id, name FROM junctions");
    while (stmt.Step()) {
      const std::string id = stmt.GetColumnText(0);
      const std::string name = stmt.GetColumnText(1);
      maliput_sparse::parser::Junction junction;
      junction.id = maliput_sparse::parser::Junction::Id(id);
      std::cerr << "Parsed Junction: " << id << " (" << name << ")" << std::endl;
      junctions_.emplace(junction.id, junction);
    }
  }

  // 2. Parse Segments
  {
    SqliteStatement stmt(db.get(), "SELECT segment_id, junction_id, name FROM segments");
    while (stmt.Step()) {
      const std::string id = stmt.GetColumnText(0);
      const std::string junction_id = stmt.GetColumnText(1);
      const std::string name = stmt.GetColumnText(2);

      maliput_sparse::parser::Segment segment;
      segment.id = maliput_sparse::parser::Segment::Id(id);

      auto junction_it = junctions_.find(maliput_sparse::parser::Junction::Id(junction_id));
      if (junction_it != junctions_.end()) {
        std::cerr << "Parsed Segment: " << id << " (" << name << ")" << std::endl;
        junction_it->second.segments.emplace(segment.id, segment);
      } else {
        std::cerr << "Warning: Segment " << id << " references unknown junction " << junction_id << std::endl;
      }
    }
  }

  // 2.5 Parse Lane Boundaries
  std::unordered_map<std::string, maliput_sparse::geometry::LineString3d> boundaries;
  {
    SqliteStatement stmt(db.get(), "SELECT boundary_id, geometry FROM lane_boundaries");
    while (stmt.Step()) {
      std::string id = stmt.GetColumnText(0);
      const void* blob = stmt.GetColumnBlob(1);
      int bytes = stmt.GetColumnBytes(1);
      std::cerr << "Parsed Boundary: " << id << std::endl;
      boundaries.emplace(id, maliput_sparse::geometry::LineString3d(ParseGeopackageGeometry(blob, bytes)));
    }
  }

  // 3. Parse Lanes
  {
    SqliteStatement stmt(db.get(), "SELECT lane_id, segment_id, left_boundary_id, right_boundary_id FROM lanes");
    while (stmt.Step()) {
      const std::string id = stmt.GetColumnText(0);
      const std::string segment_id = stmt.GetColumnText(1);
      const std::string left_boundary_id = stmt.GetColumnText(2);
      const std::string right_boundary_id = stmt.GetColumnText(3);

      if (boundaries.find(left_boundary_id) == boundaries.end() ||
          boundaries.find(right_boundary_id) == boundaries.end()) {
        std::cerr << "Warning: Lane " << id << " references unknown boundaries." << std::endl;
        continue;
      }

      maliput_sparse::parser::Lane lane{
          maliput_sparse::parser::Lane::Id(id),
          boundaries.at(left_boundary_id),
          boundaries.at(right_boundary_id),
          std::nullopt,  // left_lane_id
          std::nullopt,  // right_lane_id
          {},            // successors
          {}             // predecessors
      };

      // Find the segment to add the lane to.
      bool found = false;
      for (auto& junction_pair : junctions_) {
        auto& segments = junction_pair.second.segments;
        auto segment_it = segments.find(maliput_sparse::parser::Segment::Id(segment_id));
        if (segment_it != segments.end()) {
          segment_it->second.lanes.push_back(lane);
          found = true;
          break;
        }
      }
      if (!found) {
        std::cerr << "Warning: Lane " << id << " references unknown segment " << segment_id << std::endl;
      }
    }
  }

  // 4. Parse Connections (Branch Points) - Skipped for now as struct definition is mismatched.
  // TODO: Implement connection parsing once maliput_sparse::parser::Connection definition is confirmed.
}

GeoPackageParser::~GeoPackageParser() = default;

const std::unordered_map<maliput_sparse::parser::Junction::Id, maliput_sparse::parser::Junction>&
GeoPackageParser::DoGetJunctions() const {
  return junctions_;
}

const std::vector<maliput_sparse::parser::Connection>& GeoPackageParser::DoGetConnections() const {
  return connections_;
}

}  // namespace geopackage
}  // namespace maliput_geopackage
