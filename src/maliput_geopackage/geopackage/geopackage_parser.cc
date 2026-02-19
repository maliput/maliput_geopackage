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
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <maliput/common/logger.h>
#include <maliput/common/maliput_throw.h>
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
  // Minimum: 8 (header) + 1 (byte order) + 4 (type) + 4 (num_points) + 4 (at least 1 point: 2*8)
  MALIPUT_VALIDATE(data != nullptr, "GeoPackage geometry data is null.");
  MALIPUT_VALIDATE(bytes >= 21, "GeoPackage geometry data size " + std::to_string(bytes) + " is too small.");

  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  const uint8_t* end = ptr + bytes;

  // Skip GeoPackage Header (8 bytes for no envelope, flags=0x01)
  // Magic (2), Version (1), Flags (1), SRS_ID (4)
  ptr += 8;
  // Need at least 9 more bytes (1 + 4 + 4)
  MALIPUT_VALIDATE(ptr <= end - 9, "Not enough data in GeoPackage geometry after header.");

  // WKB Geometry
  // Byte order (1 byte)
  uint8_t byte_order = *ptr;
  ptr += 1;

  // We assume Little Endian (1) as per the python script generator.
  // A full implementation would handle Big Endian (0).
  MALIPUT_VALIDATE(byte_order == 1, "Unsupported byte order " + std::to_string(static_cast<int>(byte_order)) +
                                        " in GeoPackage geometry.");

  // Type (4 bytes)
  MALIPUT_VALIDATE(ptr <= end - 4, "Not enough data in GeoPackage geometry for WKB type.");
  uint32_t wkb_type;
  std::memcpy(&wkb_type, ptr, 4);
  ptr += 4;

  // Mask off type flags (Z, M, etc.) to get the base geometry type
  // Bit 0x80000000 indicates Z coordinate, 0x40000000 indicates M coordinate
  uint32_t base_type = wkb_type & 0x0FFFFFFF;

  // wkbLineString = 2
  MALIPUT_VALIDATE(base_type == 2, "Unsupported WKB geometry type " + std::to_string(base_type) +
                                       " (raw type: " + std::to_string(wkb_type) + ").");

  // Num points (4 bytes)
  MALIPUT_VALIDATE(ptr <= end - 4, "Not enough data in GeoPackage geometry for point count.");
  uint32_t num_points;
  std::memcpy(&num_points, ptr, 4);
  ptr += 4;

  // Validate we have enough data for all points
  MALIPUT_VALIDATE(num_points <= 1000000, "Unreasonable number of points " + std::to_string(num_points) +
                                              " in GeoPackage geometry.");  // Sanity check
  MALIPUT_VALIDATE(ptr <= end - (static_cast<int>(num_points) * 16),
                   "Not enough data in GeoPackage geometry for " + std::to_string(num_points) + " points.");

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
      MALIPUT_VALIDATE(junction_it != junctions_.end(),
                       "Segment " + id + " references unknown junction " + junction_id + ".");
      junction_it->second.segments.emplace(segment.id, segment);
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
      boundaries.emplace(id, maliput_sparse::geometry::LineString3d(ParseGeopackageGeometry(blob, bytes)));
    }
  }

  std::unordered_set<std::string> lane_ids;
  // 3. Parse Lanes
  {
    SqliteStatement stmt(db.get(), "SELECT lane_id, segment_id, left_boundary_id, right_boundary_id FROM lanes");
    while (stmt.Step()) {
      const std::string id = stmt.GetColumnText(0);
      lane_ids.insert(id);
      const std::string segment_id = stmt.GetColumnText(1);
      const std::string left_boundary_id = stmt.GetColumnText(2);
      const std::string right_boundary_id = stmt.GetColumnText(3);

      MALIPUT_VALIDATE(boundaries.find(left_boundary_id) != boundaries.end(),
                       "Lane " + id + " references unknown left boundary " + left_boundary_id + ".");
      MALIPUT_VALIDATE(boundaries.find(right_boundary_id) != boundaries.end(),
                       "Lane " + id + " references unknown right boundary " + right_boundary_id + ".");

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
      MALIPUT_VALIDATE(found, "Lane " + id + " references unknown segment " + segment_id + ".");
    }
  }

  // 3.5 Parse Adjacency and Reorder Lanes
  {
    SqliteStatement stmt(db.get(), "SELECT lane_id, adjacent_lane_id, side FROM view_adjacent_lanes");
    std::unordered_map<std::string, std::string> left_adj;
    std::unordered_map<std::string, std::string> right_adj;

    while (stmt.Step()) {
      const std::string lane_id = stmt.GetColumnText(0);
      const std::string adj_id = stmt.GetColumnText(1);
      const std::string side = stmt.GetColumnText(2);

      if (side == "left") {
        left_adj[lane_id] = adj_id;
      } else if (side == "right") {
        right_adj[lane_id] = adj_id;
      }
    }

    for (auto& [j_id, junction] : junctions_) {
      for (auto& [s_id, segment] : junction.segments) {
        // Update adjacency
        for (auto& lane : segment.lanes) {
          const std::string l_id = lane.id;
          if (left_adj.find(l_id) != left_adj.end()) {
            lane.left_lane_id = left_adj[l_id];
          }
          if (right_adj.find(l_id) != right_adj.end()) {
            lane.right_lane_id = right_adj[l_id];
          }
        }

        // Reorder lanes: Rightmost first
        if (segment.lanes.size() > 1) {
          std::vector<maliput_sparse::parser::Lane> ordered_lanes;
          ordered_lanes.reserve(segment.lanes.size());

          std::unordered_map<std::string, const maliput_sparse::parser::Lane*> lane_map;
          for (const auto& lane : segment.lanes) {
            lane_map[lane.id] = &lane;
          }

          const maliput_sparse::parser::Lane* current = nullptr;
          // Find the rightmost lane (no right_lane_id)
          for (const auto& lane : segment.lanes) {
            if (!lane.right_lane_id.has_value()) {
              current = &lane;
              break;
            }
          }

          if (current) {
            while (current) {
              ordered_lanes.push_back(*current);
              if (current->left_lane_id.has_value()) {
                auto it = lane_map.find(current->left_lane_id.value());
                current = (it != lane_map.end()) ? it->second : nullptr;
              } else {
                current = nullptr;
              }
            }

            if (ordered_lanes.size() == segment.lanes.size()) {
              segment.lanes = std::move(ordered_lanes);
            } else {
              maliput::log()->warn("Failed to strictly reorder lanes in segment ", s_id, ". Expected ",
                                   segment.lanes.size(), " lanes, found ", ordered_lanes.size(), " in chain.");
            }
          } else {
            maliput::log()->warn("Could not find rightmost lane in segment ", s_id);
          }
        }
      }
    }
  }

  // 4. Parse Connections (Branch Points)
  {
    struct BranchPointLane {
      std::string lane_id;
      maliput_sparse::parser::LaneEnd::Which end;
      std::string side;
    };
    std::unordered_map<std::string, std::vector<BranchPointLane>> branch_points;

    SqliteStatement stmt(db.get(), "SELECT branch_point_id, lane_id, lane_end, side FROM branch_point_lanes");
    while (stmt.Step()) {
      const std::string branch_point_id = stmt.GetColumnText(0);
      const std::string lane_id = stmt.GetColumnText(1);
      const std::string end_str = stmt.GetColumnText(2);
      const std::string side = stmt.GetColumnText(3);

      MALIPUT_VALIDATE(lane_ids.find(lane_id) != lane_ids.end(), "Connection references unknown lane " + lane_id + ".");

      maliput_sparse::parser::LaneEnd::Which end;
      if (end_str == "Start" || end_str == "start") {
        end = maliput_sparse::parser::LaneEnd::Which::kStart;
      } else if (end_str == "End" || end_str == "Finish" || end_str == "finish") {
        end = maliput_sparse::parser::LaneEnd::Which::kFinish;
      } else {
        MALIPUT_THROW_MESSAGE("Unknown lane end '" + end_str + "' in branch_point_lanes.");
      }

      branch_points[branch_point_id].push_back({lane_id, end, side});
    }

    for (const auto& [bp_id, bp_lanes] : branch_points) {
      std::vector<BranchPointLane> side_a;
      std::vector<BranchPointLane> side_b;
      for (const auto& bpl : bp_lanes) {
        if (bpl.side == "a") {
          side_a.push_back(bpl);
        } else if (bpl.side == "b") {
          side_b.push_back(bpl);
        }
      }

      for (const auto& a : side_a) {
        for (const auto& b : side_b) {
          connections_.push_back({{maliput_sparse::parser::Lane::Id(a.lane_id), a.end},
                                  {maliput_sparse::parser::Lane::Id(b.lane_id), b.end}});
        }
      }
    }
  }
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
