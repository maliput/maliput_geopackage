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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sqlite3.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {

namespace {

// Helper class to create and manage a temporary GeoPackage file for testing.
class TempGeoPackage {
 public:
  TempGeoPackage() {
    // Use a random filename to avoid collisions.
    filename_ = "temp_geopackage_" + std::to_string(std::rand()) + ".gpkg";
    std::remove(filename_.c_str());

    int rc = sqlite3_open(filename_.c_str(), &db_);
    if (rc != SQLITE_OK) {
      throw std::runtime_error("Failed to open temp db: " + filename_);
    }
    InitializeTables();
  }

  ~TempGeoPackage() {
    if (db_) {
      sqlite3_close(db_);
    }
    std::remove(filename_.c_str());
  }

  const std::string& GetPath() const { return filename_; }

  void ExecuteSql(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::string msg = err_msg;
      sqlite3_free(err_msg);
      throw std::runtime_error("SQL error: " + msg + " in " + sql);
    }
  }

  void DropTable(const std::string& table_name) { ExecuteSql("DROP TABLE IF EXISTS " + table_name); }

  void InsertLaneBoundary(const std::string& id, const std::vector<uint8_t>& geometry) {
    sqlite3_stmt* stmt;
    std::string sql = "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) throw std::runtime_error("Prepare failed");

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, geometry.data(), static_cast<int>(geometry.size()), SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) throw std::runtime_error("Step failed");
    sqlite3_finalize(stmt);
  }

 private:
  void InitializeTables() {
    ExecuteSql("CREATE TABLE maliput_metadata (key TEXT, value TEXT)");
    ExecuteSql("CREATE TABLE junctions (junction_id TEXT, name TEXT)");
    ExecuteSql("CREATE TABLE segments (segment_id TEXT, junction_id TEXT, name TEXT)");
    ExecuteSql("CREATE TABLE lane_boundaries (boundary_id TEXT, geometry BLOB)");
    ExecuteSql(
        "CREATE TABLE lanes (lane_id TEXT, segment_id TEXT, lane_type TEXT, direction TEXT, left_boundary_id TEXT, "
        "left_boundary_inverted INTEGER, right_boundary_id TEXT, right_boundary_inverted INTEGER)");
    ExecuteSql("CREATE TABLE branch_point_lanes (branch_point_id TEXT, lane_id TEXT, side TEXT, lane_end TEXT)");
    ExecuteSql("CREATE TABLE view_adjacent_lanes (lane_id TEXT, adjacent_lane_id TEXT, side TEXT)");
  }

  std::string filename_;
  sqlite3* db_{nullptr};
};

// Helper to build a GeoPackage geometry blob.
std::vector<uint8_t> BuildGeometryBlob(uint8_t magic0, uint8_t magic1, uint8_t version, uint8_t flags, int32_t srs_id,
                                       uint8_t byte_order, uint32_t wkb_type, uint32_t num_points, bool use_z = false) {
  std::vector<uint8_t> blob;
  // GeoPackage Header
  blob.push_back(magic0);
  blob.push_back(magic1);
  blob.push_back(version);
  blob.push_back(flags);
  // SRS ID (Little Endian)
  blob.push_back(srs_id & 0xFF);
  blob.push_back((srs_id >> 8) & 0xFF);
  blob.push_back((srs_id >> 16) & 0xFF);
  blob.push_back((srs_id >> 24) & 0xFF);

  // WKB Byte Order
  blob.push_back(byte_order);

  // WKB Type (Little Endian)
  blob.push_back(wkb_type & 0xFF);
  blob.push_back((wkb_type >> 8) & 0xFF);
  blob.push_back((wkb_type >> 16) & 0xFF);
  blob.push_back((wkb_type >> 24) & 0xFF);

  // Num Points (Little Endian)
  blob.push_back(num_points & 0xFF);
  blob.push_back((num_points >> 8) & 0xFF);
  blob.push_back((num_points >> 16) & 0xFF);
  blob.push_back((num_points >> 24) & 0xFF);

  // Points
  for (uint32_t i = 0; i < num_points; ++i) {
    // X
    double val = 1.0 * i;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&val);
    for (int j = 0; j < 8; ++j) blob.push_back(p[j]);
    // Y
    val = 2.0 * i;
    p = reinterpret_cast<const uint8_t*>(&val);
    for (int j = 0; j < 8; ++j) blob.push_back(p[j]);
    // Z
    if (use_z) {
      val = 3.0 * i;
      p = reinterpret_cast<const uint8_t*>(&val);
      for (int j = 0; j < 8; ++j) blob.push_back(p[j]);
    }
  }
  return blob;
}

}  // namespace

class GeoPackageParserTest : public ::testing::Test {
 protected:
  const std::string kDatabasePath{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};
};

TEST_F(GeoPackageParserTest, LoadValidFile) { EXPECT_NO_THROW(GeoPackageParser{kDatabasePath}); }

TEST_F(GeoPackageParserTest, LoadNonExistentFile) {
  EXPECT_THROW(GeoPackageParser("non_existent_file.gpkg"), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, Parse) {
  // Verify that the parser can load the database and parse the tables without throwing.
  GeoPackageParser parser(kDatabasePath);

  // Verify metadata.
  const std::unordered_map<std::string, std::string>& metadata = parser.GetMetadata();
  EXPECT_FALSE(metadata.empty());

  auto it_version = metadata.find("schema_version");
  ASSERT_NE(metadata.end(), it_version);
  EXPECT_EQ("1.0.0", it_version->second);

  auto it_tolerance = metadata.find("linear_tolerance");
  ASSERT_NE(metadata.end(), it_tolerance);
  EXPECT_EQ("0.01", it_tolerance->second);

  // Verify junctions.
  const auto& junctions = parser.GetJunctions();
  ASSERT_EQ(1u, junctions.size());
  auto it_j1 = junctions.find("j1");
  ASSERT_NE(junctions.end(), it_j1);
  EXPECT_EQ("Main Junction", it_j1->second.name);

  // Verify segments.
  const auto& segments = parser.GetSegments();
  ASSERT_EQ(1u, segments.size());
  auto it_seg1 = segments.find("seg1");
  ASSERT_NE(segments.end(), it_seg1);
  EXPECT_EQ("Straight Segment", it_seg1->second.name);

  // Verify lane boundaries.
  const auto& boundaries = parser.GetLaneBoundaries();
  ASSERT_EQ(3u, boundaries.size());
  auto it_b_left = boundaries.find("b_left_outer");
  ASSERT_NE(boundaries.end(), it_b_left);
  ASSERT_EQ(2u, it_b_left->second.geometry.size());
  EXPECT_DOUBLE_EQ(0.0, it_b_left->second.geometry[0].x());
  EXPECT_DOUBLE_EQ(3.5, it_b_left->second.geometry[0].y());
  EXPECT_DOUBLE_EQ(100.0, it_b_left->second.geometry[1].x());
  EXPECT_DOUBLE_EQ(3.5, it_b_left->second.geometry[1].y());

  // Verify lanes.
  const auto& lanes = parser.GetLanes();
  ASSERT_EQ(2u, lanes.size());
  auto it_lane1 = lanes.find("lane_1");
  ASSERT_NE(lanes.end(), it_lane1);
  EXPECT_EQ("seg1", it_lane1->second.segment_id);
  EXPECT_EQ("driving", it_lane1->second.lane_type);
  EXPECT_EQ("forward", it_lane1->second.direction);
  EXPECT_EQ("b_left_outer", it_lane1->second.left_boundary_id);
  EXPECT_FALSE(it_lane1->second.left_boundary_inverted);
  EXPECT_EQ("b_center", it_lane1->second.right_boundary_id);
  EXPECT_FALSE(it_lane1->second.right_boundary_inverted);

  // Verify branch point lanes.
  const auto& branch_point_lanes = parser.GetBranchPointLanes();
  ASSERT_EQ(2u, branch_point_lanes.size());
  auto it_bp_start = branch_point_lanes.find("bp_start");
  ASSERT_NE(branch_point_lanes.end(), it_bp_start);
  ASSERT_EQ(2u, it_bp_start->second.size());
  for (const auto& bp : it_bp_start->second) {
    EXPECT_EQ("a", bp.side);
    EXPECT_EQ("start", bp.lane_end);
  }

  // Verify adjacent lanes.
  const auto& adjacent_lanes = parser.GetAdjacentLanes();
  ASSERT_EQ(2u, adjacent_lanes.size());
  auto it_lane1_adj = adjacent_lanes.find("lane_1");
  ASSERT_NE(adjacent_lanes.end(), it_lane1_adj);
  bool found_l1_l2 = false;
  for (const auto& adj : it_lane1_adj->second) {
    if (adj.adjacent_lane_id == "lane_2") {
      EXPECT_EQ("right", adj.side);
      found_l1_l2 = true;
    }
  }
  EXPECT_TRUE(found_l1_l2);
}

TEST_F(GeoPackageParserTest, ParseMissingTable) {
  TempGeoPackage temp_gpkg;
  temp_gpkg.DropTable("junctions");
  EXPECT_THROW(GeoPackageParser(temp_gpkg.GetPath()), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, ParseInvalidGeometryMagic) {
  TempGeoPackage temp_gpkg;
  // Magic 'XX' instead of 'GP'
  auto blob = BuildGeometryBlob('X', 'X', 0, 0, 0, 1, 2, 0);
  temp_gpkg.InsertLaneBoundary("b1", blob);
  EXPECT_THROW(GeoPackageParser(temp_gpkg.GetPath()), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, ParseInvalidGeometryVersion) {
  TempGeoPackage temp_gpkg;
  // Version 1 (only 0 supported)
  auto blob = BuildGeometryBlob('G', 'P', 1, 0, 0, 1, 2, 0);
  temp_gpkg.InsertLaneBoundary("b1", blob);
  EXPECT_THROW(GeoPackageParser(temp_gpkg.GetPath()), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, ParseInvalidWKBByteOrder) {
  TempGeoPackage temp_gpkg;
  // Byte order 0 (Big Endian) - only Little Endian (1) supported
  auto blob = BuildGeometryBlob('G', 'P', 0, 0, 0, 0, 2, 0);
  temp_gpkg.InsertLaneBoundary("b1", blob);
  EXPECT_THROW(GeoPackageParser(temp_gpkg.GetPath()), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, ParseInvalidWKBGeometryType) {
  TempGeoPackage temp_gpkg;
  // Type 1 (Point) - only LineString (2) supported
  auto blob = BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 1, 0);
  temp_gpkg.InsertLaneBoundary("b1", blob);
  EXPECT_THROW(GeoPackageParser(temp_gpkg.GetPath()), maliput::common::assertion_error);
}

TEST_F(GeoPackageParserTest, ParseGeometryWithZ) {
  TempGeoPackage temp_gpkg;
  // Type 2 | 0x80000000 (LineStringZ), 2 points
  auto blob = BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true);
  temp_gpkg.InsertLaneBoundary("b1", blob);

  GeoPackageParser parser(temp_gpkg.GetPath());
  const auto& boundaries = parser.GetLaneBoundaries();
  ASSERT_EQ(1u, boundaries.size());
  const auto& points = boundaries.at("b1").geometry;
  ASSERT_EQ(2u, points.size());

  EXPECT_DOUBLE_EQ(0.0, points[0].x());
  EXPECT_DOUBLE_EQ(0.0, points[0].y());
  EXPECT_DOUBLE_EQ(0.0, points[0].z());

  EXPECT_DOUBLE_EQ(1.0, points[1].x());
  EXPECT_DOUBLE_EQ(2.0, points[1].y());
  EXPECT_DOUBLE_EQ(3.0, points[1].z());
}

TEST_F(GeoPackageParserTest, ParseGeometryWithoutZ) {
  TempGeoPackage temp_gpkg;
  // Type 2 (LineString), 1 point
  auto blob = BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2, 1, false);
  temp_gpkg.InsertLaneBoundary("b1", blob);

  GeoPackageParser parser(temp_gpkg.GetPath());
  const auto& boundaries = parser.GetLaneBoundaries();
  ASSERT_EQ(1u, boundaries.size());
  const auto& points = boundaries.at("b1").geometry;
  ASSERT_EQ(1u, points.size());

  EXPECT_DOUBLE_EQ(0.0, points[0].x());
  EXPECT_DOUBLE_EQ(0.0, points[0].y());
  EXPECT_DOUBLE_EQ(0.0, points[0].z());
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
