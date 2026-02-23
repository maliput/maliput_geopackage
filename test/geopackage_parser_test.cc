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

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage