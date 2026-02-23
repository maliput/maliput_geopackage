// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota
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
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <maliput/common/maliput_copyable.h>

#include "maliput/math/vector.h"
#include "maliput_geopackage/geopackage/sqlite_helpers.h"

namespace maliput_geopackage {
namespace geopackage {

/// Structures to hold the data parsed from the GeoPackage file. These structures mirror the tables in the GeoPackage.

/// Junctions table containing information about junctions in the road network.
struct GPKGJunction {
  std::string name;
};

/// Segments table containing information about segments in the road network.
struct GPKGSegment {
  std::string junction_id;
  std::string name;
};

/// Lane boundaries table containing information about boundaries in the road network.
struct GPKGLaneBoundary {
  std::vector<maliput::math::Vector3> geometry;
};

/// Lanes table containing information about lanes in the road network.
struct GPKGLane {
  std::string segment_id;
  std::string lane_type;
  std::string direction;
  std::string left_boundary_id;
  bool left_boundary_inverted;
  std::string right_boundary_id;
  bool right_boundary_inverted;
};

/// Branch point lanes table containing information about the lanes that are connected at branch points in the road
/// network.
struct GPKGBranchPointLane {
  std::string lane_id;
  std::string side;      // 'a' or 'b'
  std::string lane_end;  // 'start' or 'finish'
};

/// Adjacent lanes table containing information about the lanes that are adjacent to each other in the road network.
struct GPKGAdjacentLane {
  std::string adjacent_lane_id;
  std::string side;  // 'left' or 'right'
};

/// GeoPackageParser is responsible for loading a GeoPackage file, parsing it and filling temporary data structures to
/// hold the geopackage information.
class GeoPackageParser {
 public:
  MALIPUT_NO_COPY_NO_MOVE_NO_ASSIGN(GeoPackageParser)

  /// Constructs a GeoPackageParser object.
  /// @param gpkg_file_path The path to the GeoPackage file to load.
  /// @throws std::runtime_error if the file cannot be opened or parsed.
  explicit GeoPackageParser(const std::string& gpkg_file_path);

  /// Destructor.
  ~GeoPackageParser();

  // Getter methods for the data structures. These will be used by the GeoPackageManager to populate the
  // maliput_sparse::parser::Junctions and Connections.
  const std::unordered_map<std::string, std::string>& GetMetadata() const { return maliput_metadata_; }
  const std::unordered_map<std::string, GPKGJunction>& GetJunctions() const { return junctions_; }
  const std::unordered_map<std::string, GPKGSegment>& GetSegments() const { return segments_; }
  const std::unordered_map<std::string, GPKGLaneBoundary>& GetLaneBoundaries() const { return lane_boundaries_; }
  const std::unordered_map<std::string, GPKGLane>& GetLanes() const { return lanes_; }
  const std::unordered_map<std::string, std::vector<GPKGAdjacentLane>>& GetAdjacentLanes() const {
    return adjacent_lanes_;
  }
  const std::unordered_map<std::string, std::vector<GPKGBranchPointLane>>& GetBranchPointLanes() const {
    return branch_point_lanes_;
  }

 private:
  // Data structures to hold the parsed data from the GeoPackage file
  /// Metadata key-value pairs from the maliput_metadata table.
  std::unordered_map<std::string, std::string> maliput_metadata_;
  /// Junctions parsed from the junctions table. Keyed by junction_id.
  std::unordered_map<std::string, GPKGJunction> junctions_;
  /// Segments parsed from the segments table. Keyed by segment_id.
  std::unordered_map<std::string, GPKGSegment> segments_;
  /// Lane boundaries parsed from the lane_boundaries table. Keyed by boundary_id.
  std::unordered_map<std::string, GPKGLaneBoundary> lane_boundaries_;
  /// Lanes parsed from the lanes table. Keyed by lane_id.
  std::unordered_map<std::string, GPKGLane> lanes_;
  /// Branch point lanes parsed from the branch_point_lanes table. Keyed by branch_point_id with multiple lanes per
  /// branch point.
  std::unordered_map<std::string, std::vector<GPKGBranchPointLane>> branch_point_lanes_;
  /// Adjacent lanes parsed from the adjacent_lanes table. Keyed by lane_id with multiple adjacent lanes per lane.
  std::unordered_map<std::string, std::vector<GPKGAdjacentLane>> adjacent_lanes_;

  /// Opens the GeoPackage database.
  /// @param gpkg_file_path The path to the GeoPackage file to load.
  /// @returns A SqliteDatabase object representing the opened database connection.
  /// @throws std::runtime_error if the file cannot be opened.
  SqliteDatabase LoadDatabase(const std::string& gpkg_file_path) const;

  /// Parses the metadata from the GeoPackage.
  std::unordered_map<std::string, std::string> ParseMetadata(const SqliteDatabase& db) const;

  /// Parses the junctions from the GeoPackage.
  std::unordered_map<std::string, GPKGJunction> ParseJunctions(const SqliteDatabase& db) const;

  /// Parses the segments from the GeoPackage.
  std::unordered_map<std::string, GPKGSegment> ParseSegments(const SqliteDatabase& db) const;

  /// Parses the lane boundaries from the GeoPackage and stores them in the boundary_lines_ member variable.
  std::unordered_map<std::string, GPKGLaneBoundary> ParseBoundaries(const SqliteDatabase& db) const;

  /// Converts a GeoPackage geometry blob to a vector of Vector3 points.
  /// Supports GeoPackage 1.0.0+ WKB format with LINESTRING geometries.
  /// The implementation follows the GeoPackage standard encoding:
  /// - GeoPackage magic header ("GP")
  /// - Envelope support (XY, XYZ, XYM, XYZM)
  /// - Little-endian WKB (Well-Known Binary) encoding
  /// - LINESTRING geometry type only
  /// @param data The data blob containing the geometry.
  /// @param bytes The size of the data blob.
  /// @returns A vector of Vector3 points representing the geometry.
  /// @throws std::runtime_error if the geometry format is invalid or unsupported.
  std::vector<maliput::math::Vector3> ParseGeopackageGeometry(const void* data, int bytes) const;

  /// Parses the lanes from the GeoPackage.
  std::unordered_map<std::string, GPKGLane> ParseLanes(const SqliteDatabase& db) const;

  /// Parses the branch point lanes from the GeoPackage and builds the connections between lanes.
  std::unordered_map<std::string, std::vector<GPKGBranchPointLane>> ParseBranchPoints(const SqliteDatabase& db) const;

  /// Parses the adjacent lanes from the GeoPackage.
  std::unordered_map<std::string, std::vector<GPKGAdjacentLane>> ParseAdjacentLanes(const SqliteDatabase& db) const;
};

}  // namespace geopackage
}  // namespace maliput_geopackage
