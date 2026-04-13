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
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT of THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "maliput_geopackage/geopackage/geopackage_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <maliput/common/maliput_abort.h>
#include <maliput_sparse/parser/parser.h>
#include <sqlite3.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {
namespace {

// Helper class to create a temporary GeoPackage file for testing.
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
      std::string msg = err_msg ? err_msg : "";
      sqlite3_free(err_msg);
      throw std::runtime_error("SQL error: " + msg + " in " + sql);
    }
  }

  void InsertLaneBoundary(const std::string& id, const std::vector<uint8_t>& geometry) {
    sqlite3_stmt* stmt = nullptr;
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
    ExecuteSql(
        "CREATE TABLE speed_limits (speed_limit_id TEXT, lane_id TEXT, s_start REAL, s_end REAL, max_speed REAL, "
        "min_speed REAL DEFAULT 0.0, description TEXT, severity INTEGER DEFAULT 0)");
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

// Provides a list of all gpkg files in the test/resources directory.
std::vector<std::string> GetGpkgFiles() {
  return {
      "complex_road.gpkg", "s_shape_superelevated_road.gpkg", "t_shape_road.gpkg", "Town01.gpkg", "two_lane_road.gpkg",
  };
}

class GeoPackageManagerTest : public ::testing::TestWithParam<std::string> {
 protected:
  const std::string kDatabasePath{std::string(TEST_RESOURCES_DIR) + "/" + GetParam()};
};

TEST_P(GeoPackageManagerTest, LoadAndCheckAPI) {
  GeoPackageManager dut{kDatabasePath};

  // The test fixtures should contain at least one junction.
  const auto& junctions = dut.GetJunctions();
  EXPECT_FALSE(junctions.empty());

  // Connections are not guaranteed to be present in the current implementation.
  (void)dut.GetConnections();

  // By default the test fixtures do not include geo_reference_info.
  const auto& geo_ref = dut.GetGeoReferenceInfo();
  EXPECT_TRUE(geo_ref.empty());
}

INSTANTIATE_TEST_CASE_P(GeoPackageManagerTests, GeoPackageManagerTest, ::testing::ValuesIn(GetGpkgFiles()));

TEST(GeoPackageManagerTest, TwoLaneRoadBuildsExpectedStructure) {
  GeoPackageManager dut{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};

  const auto& junctions = dut.GetJunctions();
  ASSERT_EQ(1u, junctions.size());
  auto it_j1 = junctions.find("j1");
  ASSERT_NE(junctions.end(), it_j1);

  const auto& junction = it_j1->second;
  ASSERT_EQ(1u, junction.segments.size());
  auto it_seg1 = junction.segments.find("seg1");
  ASSERT_NE(junction.segments.end(), it_seg1);

  const auto& lanes = it_seg1->second.lanes;
  ASSERT_EQ(2u, lanes.size());

  // The segment lanes are ordered from right to left.
  EXPECT_EQ("lane_2", lanes[0].id);
  EXPECT_EQ("lane_1", lanes[1].id);

  // Confirm adjacency information produced by the manager.
  EXPECT_FALSE(lanes[0].right_lane_id.has_value());
  EXPECT_TRUE(lanes[0].left_lane_id.has_value());
  EXPECT_EQ("lane_1", lanes[0].left_lane_id.value());

  EXPECT_FALSE(lanes[1].left_lane_id.has_value());
  EXPECT_TRUE(lanes[1].right_lane_id.has_value());
  EXPECT_EQ("lane_2", lanes[1].right_lane_id.value());

  // Connections are not guaranteed to be present in the current implementation.
  (void)dut.GetConnections();
}

TEST(GeoPackageManagerTest, TShapeRoadDeduplicatesConnections) {
  GeoPackageManager dut{std::string(TEST_RESOURCES_DIR) + "/t_shape_road.gpkg"};

  const auto& connections = dut.GetConnections();
  ASSERT_FALSE(connections.empty());

  // Ensure that no two connections are duplicates after canonicalization.
  auto canonical = [](const maliput_sparse::parser::Connection& c) {
    const auto a = std::make_pair(c.from.lane_id, static_cast<int>(c.from.end));
    const auto b = std::make_pair(c.to.lane_id, static_cast<int>(c.to.end));
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
  };

  std::set<decltype(canonical(connections[0]))> seen;
  for (const auto& c : connections) {
    auto key = canonical(c);
    EXPECT_TRUE(seen.insert(key).second);
  }
}

TEST(GeoPackageManagerNegativeTest, ConstructorWithNonExistentFile) {
  EXPECT_THROW(GeoPackageManager("non_existent_file.gpkg"), maliput::common::assertion_error);
}

TEST(GeoPackageManagerNegativeTest, ConstructorWithInvalidFile) {
  // Create an empty file.
  const std::string invalid_file_path = "invalid.gpkg";
  std::ofstream ofs(invalid_file_path);
  ofs.close();
  EXPECT_THROW(GeoPackageManager{invalid_file_path}, maliput::common::assertion_error);
  std::remove(invalid_file_path.c_str());
}

TEST(GeoPackageManagerNegativeTest, MissingLaneBoundaryThrows) {
  TempGeoPackage temp;
  temp.ExecuteSql("INSERT INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0.0')");
  temp.ExecuteSql("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main')");
  temp.ExecuteSql("INSERT INTO segments (segment_id, junction_id, name) VALUES ('seg1', 'j1', 'seg')");
  temp.ExecuteSql(
      "INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
      "right_boundary_id, right_boundary_inverted) VALUES ('lane_1', 'seg1', 'driving', 'forward', 'missing', 0, "
      "'missing', 0)");
  EXPECT_THROW(GeoPackageManager(temp.GetPath()), std::runtime_error);
}

TEST(GeoPackageManagerNegativeTest, InvalidLaneEndIsIgnored) {
  TempGeoPackage temp;
  temp.ExecuteSql("INSERT INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0.0')");
  temp.ExecuteSql("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main')");
  temp.ExecuteSql("INSERT INTO segments (segment_id, junction_id, name) VALUES ('seg1', 'j1', 'seg')");

  temp.InsertLaneBoundary("b1", BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true));
  temp.ExecuteSql(
      "INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
      "right_boundary_id, right_boundary_inverted) VALUES ('lane_1', 'seg1', 'driving', 'forward', 'b1', 0, 'b1', 0)");
  temp.ExecuteSql(
      "INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end) VALUES ('bp', 'lane_1', 'a', "
      "'middle')");

  // Invalid lane_end values are ignored by the current implementation.
  EXPECT_NO_THROW(GeoPackageManager(temp.GetPath()));
}

TEST(GeoPackageManagerTest, GeoReferenceInfoIsReturned) {
  TempGeoPackage temp;
  temp.ExecuteSql("INSERT INTO maliput_metadata (key, value) VALUES ('geo_reference_info', 'EPSG:1234')");
  temp.ExecuteSql("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main')");
  temp.ExecuteSql("INSERT INTO segments (segment_id, junction_id, name) VALUES ('seg1', 'j1', 'seg')");
  temp.InsertLaneBoundary("b1", BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true));
  temp.ExecuteSql(
      "INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
      "right_boundary_id, right_boundary_inverted) VALUES ('lane_1', 'seg1', 'driving', 'forward', 'b1', 0, 'b1', 0)");

  GeoPackageManager dut{temp.GetPath()};
  EXPECT_EQ("EPSG:1234", dut.GetGeoReferenceInfo());
}

// -- Speed limit tests ---------------------------------------------------------

TEST(GeoPackageManagerTest, SpeedLimitsAreWiredToLanes) {
  TempGeoPackage temp;
  temp.ExecuteSql("INSERT INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0.0')");
  temp.ExecuteSql("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main')");
  temp.ExecuteSql("INSERT INTO segments (segment_id, junction_id, name) VALUES ('seg1', 'j1', 'seg')");
  temp.InsertLaneBoundary("b_left", BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true));
  temp.InsertLaneBoundary("b_right", BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true));
  temp.ExecuteSql(
      "INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
      "right_boundary_id, right_boundary_inverted) VALUES ('lane_1', 'seg1', 'driving', 'forward', 'b_left', 0, "
      "'b_right', 0)");
  temp.ExecuteSql("INSERT INTO speed_limits VALUES ('sl_1', 'lane_1', 0.0, 100.0, 13.89, 0.0, 'Urban road. [m/s]', 0)");
  temp.ExecuteSql(
      "INSERT INTO speed_limits VALUES ('sl_2', 'lane_1', 100.0, 200.0, 8.33, 2.78, 'School zone. [m/s]', 1)");

  GeoPackageManager dut{temp.GetPath()};
  const auto& junctions = dut.GetJunctions();
  ASSERT_EQ(1u, junctions.size());
  const auto& segment = junctions.at("j1").segments.at("seg1");
  ASSERT_EQ(1u, segment.lanes.size());
  const auto& lane = segment.lanes[0];
  EXPECT_EQ("lane_1", lane.id);
  ASSERT_EQ(2u, lane.speed_limits.size());

  EXPECT_DOUBLE_EQ(0.0, lane.speed_limits[0].s_start);
  EXPECT_DOUBLE_EQ(100.0, lane.speed_limits[0].s_end);
  EXPECT_DOUBLE_EQ(0.0, lane.speed_limits[0].min);
  EXPECT_DOUBLE_EQ(13.89, lane.speed_limits[0].max);
  EXPECT_EQ("Urban road. [m/s]", lane.speed_limits[0].description);
  EXPECT_EQ(0, lane.speed_limits[0].severity);

  EXPECT_DOUBLE_EQ(100.0, lane.speed_limits[1].s_start);
  EXPECT_DOUBLE_EQ(200.0, lane.speed_limits[1].s_end);
  EXPECT_DOUBLE_EQ(2.78, lane.speed_limits[1].min);
  EXPECT_DOUBLE_EQ(8.33, lane.speed_limits[1].max);
  EXPECT_EQ("School zone. [m/s]", lane.speed_limits[1].description);
  EXPECT_EQ(1, lane.speed_limits[1].severity);
}

TEST(GeoPackageManagerTest, NoSpeedLimitsProducesEmptyVector) {
  TempGeoPackage temp;
  temp.ExecuteSql("INSERT INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0.0')");
  temp.ExecuteSql("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main')");
  temp.ExecuteSql("INSERT INTO segments (segment_id, junction_id, name) VALUES ('seg1', 'j1', 'seg')");
  temp.InsertLaneBoundary("b1", BuildGeometryBlob('G', 'P', 0, 0, 0, 1, 2 | 0x80000000, 2, true));
  temp.ExecuteSql(
      "INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, left_boundary_inverted, "
      "right_boundary_id, right_boundary_inverted) VALUES ('lane_1', 'seg1', 'driving', 'forward', 'b1', 0, 'b1', 0)");

  GeoPackageManager dut{temp.GetPath()};
  const auto& lane = dut.GetJunctions().at("j1").segments.at("seg1").lanes[0];
  EXPECT_TRUE(lane.speed_limits.empty());
}

}  // namespace
}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
