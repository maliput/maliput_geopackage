#include "maliput_geopackage/geopackage/geopackage_parser.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {

class GeoPackageParserTest : public ::testing::Test {
 protected:
  const std::string kDatabasePath{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};
};

TEST_F(GeoPackageParserTest, Parse) {
  // Verify that the parser can load the database and parse the tables without throwing.
  GeoPackageParser parser(kDatabasePath);

  // Verify metadata.
  const std::unordered_map<std::string, std::string>& metadata = parser.GetMetadata();
  EXPECT_FALSE(metadata.empty());
  auto find_metadata = [&](const std::string& key) {
    auto it = metadata.find(key);
    return it;
  };

  auto it_version = find_metadata("schema_version");
  ASSERT_NE(metadata.end(), it_version);
  EXPECT_EQ("1.0.0", it_version->second);

  auto it_tolerance = find_metadata("linear_tolerance");
  ASSERT_NE(metadata.end(), it_tolerance);
  EXPECT_EQ("0.01", it_tolerance->second);

  // Verify junctions.
  const std::vector<GPKGJunction>& junctions = parser.GetJunctions();
  ASSERT_EQ(1u, junctions.size());
  EXPECT_EQ("j1", junctions[0].junction_id);
  EXPECT_EQ("Main Junction", junctions[0].name);

  // Verify segments.
  const std::vector<GPKGSegment>& segments = parser.GetSegments();
  ASSERT_EQ(1u, segments.size());
  EXPECT_EQ("seg1", segments[0].segment_id);
  EXPECT_EQ("j1", segments[0].junction_id);
  EXPECT_EQ("Straight Segment", segments[0].name);

  // Verify lane boundaries.
  const std::vector<GPKGLaneBoundary>& boundaries = parser.GetLaneBoundaries();
  ASSERT_EQ(3u, boundaries.size());
  auto find_boundary = [&](const std::string& id) {
    return std::find_if(boundaries.begin(), boundaries.end(), [&](const auto& b) { return b.boundary_id == id; });
  };
  auto it_b_left = find_boundary("b_left_outer");
  ASSERT_NE(boundaries.end(), it_b_left);
  ASSERT_EQ(2u, it_b_left->geometry.size());
  EXPECT_DOUBLE_EQ(0.0, it_b_left->geometry[0].x());
  EXPECT_DOUBLE_EQ(3.5, it_b_left->geometry[0].y());
  EXPECT_DOUBLE_EQ(100.0, it_b_left->geometry[1].x());
  EXPECT_DOUBLE_EQ(3.5, it_b_left->geometry[1].y());

  // Verify lanes.
  const std::vector<GPKGLane>& lanes = parser.GetLanes();
  ASSERT_EQ(2u, lanes.size());
  auto find_lane = [&](const std::string& id) {
    return std::find_if(lanes.begin(), lanes.end(), [&](const auto& l) { return l.lane_id == id; });
  };
  auto it_lane1 = find_lane("lane_1");
  ASSERT_NE(lanes.end(), it_lane1);
  EXPECT_EQ("seg1", it_lane1->segment_id);
  EXPECT_EQ("driving", it_lane1->lane_type);
  EXPECT_EQ("forward", it_lane1->direction);
  EXPECT_EQ("b_left_outer", it_lane1->left_boundary_id);
  EXPECT_FALSE(it_lane1->left_boundary_inverted);
  EXPECT_EQ("b_center", it_lane1->right_boundary_id);
  EXPECT_FALSE(it_lane1->right_boundary_inverted);

  // Verify branch point lanes.
  const std::vector<GPKGBranchPointLane>& branch_point_lanes = parser.GetBranchPointLanes();
  ASSERT_EQ(4u, branch_point_lanes.size());
  int bp_start_count = 0;
  for (const auto& bp : branch_point_lanes) {
    if (bp.branch_point_id == "bp_start") {
      EXPECT_EQ("a", bp.side);
      EXPECT_EQ("start", bp.lane_end);
      bp_start_count++;
    }
  }
  EXPECT_EQ(2, bp_start_count);

  // Verify adjacent lanes.
  const std::vector<GPKGAdjacentLane>& adjacent_lanes = parser.GetAdjacentLanes();
  ASSERT_EQ(2u, adjacent_lanes.size());
  bool found_l1_l2 = false;
  for (const auto& adj : adjacent_lanes) {
    if (adj.lane_id == "lane_1" && adj.adjacent_lane_id == "lane_2") {
      EXPECT_EQ("right", adj.side);
      found_l1_l2 = true;
    }
  }
  EXPECT_TRUE(found_l1_l2);
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage