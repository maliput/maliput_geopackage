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

#include <maliput/common/logger.h>
#include <maliput/common/maliput_throw.h>

namespace maliput_geopackage {
namespace geopackage {

GeoPackageParser::GeoPackageParser(const std::string& gpkg_file_path) {
  maliput::log()->trace("Parsing GeoPackage file: ", gpkg_file_path);
  auto db = LoadDatabase(gpkg_file_path);

  maliput::log()->trace("Parsing GeoPackage metadata...");
  maliput_metadata_ = ParseMetadata(db);
  maliput::log()->trace("Parsing GeoPackage junctions...");
  junctions_ = ParseJunctions(db);
  maliput::log()->trace("Parsing GeoPackage segments...");
  segments_ = ParseSegments(db);
  maliput::log()->trace("Parsing GeoPackage lane boundaries...");
  lane_boundaries_ = ParseBoundaries(db);
  maliput::log()->trace("Parsing GeoPackage lanes...");
  lanes_ = ParseLanes(db);
  maliput::log()->trace("Parsing GeoPackage branch point lanes...");
  branch_point_lanes_ = ParseBranchPoints(db);
  maliput::log()->trace("Parsing GeoPackage adjacent lanes...");
  adjacent_lanes_ = ParseAdjacentLanes(db);
}

GeoPackageParser::~GeoPackageParser() = default;

SqliteDatabase GeoPackageParser::LoadDatabase(const std::string& gpkg_file_path) const {
  // Load the database and check for errors
  return SqliteDatabase(gpkg_file_path);
}

std::unordered_map<std::string, std::string> GeoPackageParser::ParseMetadata(const SqliteDatabase& db) const {
  // Parse maliput_metadata and store
  SqliteStatement stmt(db.get(), "SELECT key, value FROM maliput_metadata");
  std::unordered_map<std::string, std::string> metadata;
  while (stmt.Step()) {
    metadata[stmt.GetColumnText(0)] = stmt.GetColumnText(1);
  }
  return metadata;
}

std::unordered_map<std::string, GPKGJunction> GeoPackageParser::ParseJunctions(const SqliteDatabase& db) const {
  // Parse junctions and store
  SqliteStatement stmt(db.get(), "SELECT junction_id, name FROM junctions");
  std::unordered_map<std::string, GPKGJunction> junctions;
  while (stmt.Step()) {
    std::string id = stmt.GetColumnText(0);
    junctions[id] = {stmt.GetColumnText(1)};
  }
  return junctions;
}

std::unordered_map<std::string, GPKGSegment> GeoPackageParser::ParseSegments(const SqliteDatabase& db) const {
  // Parse segments and store
  SqliteStatement stmt(db.get(), "SELECT segment_id, junction_id, name FROM segments");
  std::unordered_map<std::string, GPKGSegment> segments;
  while (stmt.Step()) {
    std::string id = stmt.GetColumnText(0);
    segments[id] = {stmt.GetColumnText(1), stmt.GetColumnText(2)};
  }
  return segments;
}

std::unordered_map<std::string, GPKGLaneBoundary> GeoPackageParser::ParseBoundaries(const SqliteDatabase& db) const {
  // Parse lane_boundaries and store
  SqliteStatement stmt(db.get(), "SELECT boundary_id, geometry FROM lane_boundaries");
  std::unordered_map<std::string, GPKGLaneBoundary> boundaries;
  while (stmt.Step()) {
    std::string id = stmt.GetColumnText(0);
    const void* blob = stmt.GetColumnBlob(1);
    int bytes = stmt.GetColumnBytes(1);
    boundaries[id] = {ParseGeopackageGeometry(blob, bytes)};
  }
  return boundaries;
}

std::vector<maliput::math::Vector3> GeoPackageParser::ParseGeopackageGeometry(const void* data, int bytes) const {
  // Parses GeoPackage 1.0.0+ standard geometry blobs.
  // Format: [GeoPackage Header] + [WKB Data]
  // GeoPackage Header (8 bytes minimum):
  //   - Bytes 0-1: Magic "GP"
  //   - Byte 2: Version (0 = v1.0.0+)
  //   - Byte 3: Flags (endianness, envelope type, etc.)
  //   - Bytes 4-7: SRID (SRS ID)
  //   - Optional: Envelope (0-64 bytes depending on type)
  // WKB Data: Standard ISO SQL/MM WKB format (little-endian only)
  //   - Only LINESTRING geometries (type 2) are currently supported
  //   - Z-coordinates are optional based on WKB type flags

  MALIPUT_VALIDATE(data != nullptr, "GeoPackage geometry data is null.");
  MALIPUT_VALIDATE(bytes >= 8, "GeoPackage geometry blob too small.");

  const uint8_t* ptr = static_cast<const uint8_t*>(data);
  const uint8_t* end = ptr + bytes;

  // ---- GeoPackage header ----
  // Magic "GP"
  MALIPUT_VALIDATE(ptr[0] == 'G' && ptr[1] == 'P', "Invalid GeoPackage geometry magic.");
  ptr += 2;

  // Version
  const uint8_t version = *ptr++;
  MALIPUT_VALIDATE(version == 0, "Unsupported GeoPackage geometry version.");

  // Flags
  const uint8_t flags = *ptr++;
  const uint8_t envelope_indicator = (flags >> 1) & 0x07;

  // SRS ID
  MALIPUT_VALIDATE(ptr + 4 <= end, "Truncated GeoPackage geometry SRS.");
  ptr += 4;

  // Envelope (skip if present)
  static const int envelope_sizes[] = {
      0,   // none
      32,  // XY
      48,  // XYZ
      48,  // XYM
      64   // XYZM
  };

  MALIPUT_VALIDATE(envelope_indicator <= 4, "Unsupported GeoPackage envelope type.");

  ptr += envelope_sizes[envelope_indicator];
  MALIPUT_VALIDATE(ptr < end, "Invalid GeoPackage geometry envelope.");

  // ---- WKB ----
  // Byte order
  const uint8_t byte_order = *ptr++;
  MALIPUT_VALIDATE(byte_order == 1, "Only little-endian WKB supported.");

  // Geometry type
  MALIPUT_VALIDATE(ptr + 4 <= end, "Truncated WKB geometry type.");
  uint32_t wkb_type;
  std::memcpy(&wkb_type, ptr, 4);
  ptr += 4;

  const bool has_z = (wkb_type & 0x80000000) != 0;
  const uint32_t base_type = wkb_type & 0x0FFFFFFF;

  MALIPUT_VALIDATE(base_type == 2, "Only LINESTRING geometries supported.");

  // Number of points
  MALIPUT_VALIDATE(ptr + 4 <= end, "Truncated WKB point count.");
  uint32_t num_points;
  std::memcpy(&num_points, ptr, 4);
  ptr += 4;

  const int stride = has_z ? 24 : 16;
  MALIPUT_VALIDATE(ptr + num_points * stride <= end, "Insufficient WKB point data.");

  std::vector<maliput::math::Vector3> points;
  points.reserve(num_points);

  for (uint32_t i = 0; i < num_points; ++i) {
    double x, y, z = 0.0;

    std::memcpy(&x, ptr, 8);
    ptr += 8;
    std::memcpy(&y, ptr, 8);
    ptr += 8;

    if (has_z) {
      std::memcpy(&z, ptr, 8);
      ptr += 8;
    }

    points.emplace_back(x, y, z);
  }

  return points;
}

std::unordered_map<std::string, GPKGLane> GeoPackageParser::ParseLanes(const SqliteDatabase& db) const {
  // Parse lanes and store
  SqliteStatement stmt(db.get(),
                       "SELECT lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
                       "right_boundary_id, right_boundary_inverted FROM lanes");

  std::unordered_map<std::string, GPKGLane> lanes;
  while (stmt.Step()) {
    std::string id = stmt.GetColumnText(0);
    lanes[id] = {stmt.GetColumnText(1),     stmt.GetColumnText(2), stmt.GetColumnText(3),    stmt.GetColumnText(4),
                 stmt.GetColumnInt(5) != 0, stmt.GetColumnText(6), stmt.GetColumnInt(7) != 0};
  }
  return lanes;
}

std::unordered_map<std::string, std::vector<GPKGBranchPointLane>> GeoPackageParser::ParseBranchPoints(
    const SqliteDatabase& db) const {
  // Parse branch points and build connections
  SqliteStatement stmt(db.get(), "SELECT branch_point_id, lane_id, side, lane_end FROM branch_point_lanes");

  std::unordered_map<std::string, std::vector<GPKGBranchPointLane>> gpkp_connections;
  while (stmt.Step()) {
    std::string id = stmt.GetColumnText(0);
    gpkp_connections[id].push_back({stmt.GetColumnText(1), stmt.GetColumnText(2), stmt.GetColumnText(3)});
  }
  return gpkp_connections;
}

std::unordered_map<std::string, std::vector<GPKGAdjacentLane>> GeoPackageParser::ParseAdjacentLanes(
    const SqliteDatabase& db) const {
  // Parse adjacent lanes view
  SqliteStatement stmt(db.get(), "SELECT lane_id, adjacent_lane_id, side FROM view_adjacent_lanes");
  std::unordered_map<std::string, std::vector<GPKGAdjacentLane>> adjacent_lanes;
  while (stmt.Step()) {
    std::string lane_id = stmt.GetColumnText(0);
    adjacent_lanes[lane_id].push_back({stmt.GetColumnText(1), stmt.GetColumnText(2)});
  }
  return adjacent_lanes;
}

}  // namespace geopackage
}  // namespace maliput_geopackage
