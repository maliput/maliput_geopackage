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

#include "maliput_geopackage/builder/road_network_builder.h"

#include <map>
#include <string>

#include <gtest/gtest.h>
#include <maliput/api/road_network.h>
#include <maliput/api/rules/range_value_rule.h>
#include <maliput/api/rules/road_rulebook.h>
#include <maliput/api/rules/rule.h>
#include <maliput/api/rules/speed_limit_rule.h>
#include <maliput/base/rule_registry.h>

namespace maliput_geopackage {
namespace builder {
namespace test {
namespace {

class RoadNetworkBuilderTest : public ::testing::Test {
 protected:
  // two_lane_road.gpkg has speed_limits table with 2 entries (lane_1 and lane_2).
  const std::string kGpkgFile{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};
};

TEST_F(RoadNetworkBuilderTest, BuildsRoadNetwork) {
  const std::map<std::string, std::string> config{{"gpkg_file", kGpkgFile}};
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);
  ASSERT_NE(nullptr, road_network->road_geometry());
  ASSERT_NE(nullptr, road_network->rulebook());
  ASSERT_NE(nullptr, road_network->rule_registry());
}

TEST_F(RoadNetworkBuilderTest, SpeedLimitRulesArePopulated) {
  const std::map<std::string, std::string> config{{"gpkg_file", kGpkgFile}};
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  // Verify speed limit rule type is registered.
  const auto& registry = *road_network->rule_registry();
  const auto& range_types = registry.RangeValueRuleTypes();
  auto it = range_types.find(maliput::SpeedLimitRuleTypeId());
  ASSERT_NE(range_types.end(), it);

  // Query the rulebook for speed limit rules.
  // two_lane_road.gpkg has 2 speed limit entries (one per lane).
  const auto& rulebook = *road_network->rulebook();

  // Verify rules for lane_1.
  const maliput::api::rules::Rule::Id rule_id_0(maliput::SpeedLimitRuleTypeId().string() + "/lane_1_0");
  auto result = rulebook.GetRangeValueRule(rule_id_0);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(1u, result->states().size());
  EXPECT_DOUBLE_EQ(0.0, result->states()[0].min);
  EXPECT_DOUBLE_EQ(13.89, result->states()[0].max);
  EXPECT_EQ("Urban road. [m/s]", result->states()[0].description);

  // Verify rules for lane_2.
  const maliput::api::rules::Rule::Id rule_id_1(maliput::SpeedLimitRuleTypeId().string() + "/lane_2_0");
  auto result2 = rulebook.GetRangeValueRule(rule_id_1);
  ASSERT_TRUE(result2.has_value());
  ASSERT_EQ(1u, result2->states().size());
  EXPECT_DOUBLE_EQ(0.0, result2->states()[0].min);
  EXPECT_DOUBLE_EQ(8.33, result2->states()[0].max);
  EXPECT_EQ("School zone. [m/s]", result2->states()[0].description);
}

TEST_F(RoadNetworkBuilderTest, NoSpeedLimitsProducesEmptyRulebook) {
  // complex_road.gpkg does not have a speed_limits table.
  const std::string kNoSpeedLimitsGpkg{std::string(TEST_RESOURCES_DIR) + "/complex_road.gpkg"};
  const std::map<std::string, std::string> config{{"gpkg_file", kNoSpeedLimitsGpkg}};
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  // No speed limit rules should be registered.
  const auto& registry = *road_network->rule_registry();
  const auto& range_types = registry.RangeValueRuleTypes();
  EXPECT_EQ(range_types.end(), range_types.find(maliput::SpeedLimitRuleTypeId()));
}

// Mimics the flow that maliput_query's GetMaxSpeedLimit uses:
// FindRules(lane_s_range) -> query_result.speed_limit
TEST_F(RoadNetworkBuilderTest, FindRulesReturnsSpeedLimitsForLane) {
  const std::map<std::string, std::string> config{{"gpkg_file", kGpkgFile}};
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  const auto* lane = road_network->road_geometry()->ById().GetLane(maliput::api::LaneId("lane_1"));
  ASSERT_NE(nullptr, lane);

  // Build a LaneSRange covering the full lane (same as maliput_query's FindRulesFor).
  const maliput::api::LaneSRange lane_s_range(lane->id(), maliput::api::SRange(0., lane->length()));
  const auto query_result = road_network->rulebook()->FindRules({lane_s_range}, 0.);

  // New API: RangeValueRule entries should be present.
  EXPECT_FALSE(query_result.range_value_rules.empty());
  bool found_range_rule = false;
  for (const auto& [id, rule] : query_result.range_value_rules) {
    if (id.string().find("lane_1") != std::string::npos) {
      found_range_rule = true;
      ASSERT_EQ(1u, rule.states().size());
      EXPECT_DOUBLE_EQ(13.89, rule.states()[0].max);
    }
  }
  EXPECT_TRUE(found_range_rule);

  // Deprecated API: SpeedLimitRule entries should also be present (for backward compatibility).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  EXPECT_FALSE(query_result.speed_limit.empty());
  bool found_speed_limit = false;
  for (const auto& [id, rule] : query_result.speed_limit) {
    if (id.string().find("lane_1") != std::string::npos) {
      found_speed_limit = true;
      EXPECT_DOUBLE_EQ(13.89, rule.max());
      EXPECT_DOUBLE_EQ(0.0, rule.min());
    }
  }
  EXPECT_TRUE(found_speed_limit);
#pragma GCC diagnostic pop
}

// Verifies speed limits are loaded from GPKG only (no YAML).
TEST_F(RoadNetworkBuilderTest, SpeedLimitsFromGpkgOnly) {
  const std::map<std::string, std::string> config{{"gpkg_file", kGpkgFile}};
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  const auto& rulebook = *road_network->rulebook();
  const auto& registry = *road_network->rule_registry();

  // Speed-Limit Rule Type must be registered from GPKG data.
  const auto& range_types = registry.RangeValueRuleTypes();
  ASSERT_NE(range_types.end(), range_types.find(maliput::SpeedLimitRuleTypeId()));

  // Verify GPKG-sourced rules exist.
  const maliput::api::rules::Rule::Id gpkg_rule_1(maliput::SpeedLimitRuleTypeId().string() + "/lane_1_0");
  auto result_1 = rulebook.GetRangeValueRule(gpkg_rule_1);
  ASSERT_TRUE(result_1.has_value());
  EXPECT_DOUBLE_EQ(13.89, result_1->states()[0].max);

  const maliput::api::rules::Rule::Id gpkg_rule_2(maliput::SpeedLimitRuleTypeId().string() + "/lane_2_0");
  auto result_2 = rulebook.GetRangeValueRule(gpkg_rule_2);
  ASSERT_TRUE(result_2.has_value());
  EXPECT_DOUBLE_EQ(8.33, result_2->states()[0].max);

  // Verify deprecated API also works.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  const auto* lane = road_network->road_geometry()->ById().GetLane(maliput::api::LaneId("lane_1"));
  ASSERT_NE(nullptr, lane);
  const maliput::api::LaneSRange lane_s_range(lane->id(), maliput::api::SRange(0., lane->length()));
  const auto query_result = rulebook.FindRules({lane_s_range}, 0.);
  EXPECT_FALSE(query_result.speed_limit.empty());
#pragma GCC diagnostic pop
}

// Verifies speed limits are loaded from YAML only (no GPKG speed_limits table).
TEST_F(RoadNetworkBuilderTest, SpeedLimitsFromYamlOnly) {
  // complex_road.gpkg has no speed_limits table.
  const std::string kNoSpeedLimitsGpkg{std::string(TEST_RESOURCES_DIR) + "/complex_road.gpkg"};
  const std::string kYamlFile{std::string(TEST_RESOURCES_DIR) + "/complex_road_speed_limits.yaml"};

  const std::map<std::string, std::string> config{
      {"gpkg_file", kNoSpeedLimitsGpkg},
      {"rule_registry", kYamlFile},
      {"road_rule_book", kYamlFile},
  };
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  const auto& rulebook = *road_network->rulebook();
  const auto& registry = *road_network->rule_registry();

  // Speed-Limit Rule Type must be registered from YAML.
  const auto& range_types = registry.RangeValueRuleTypes();
  ASSERT_NE(range_types.end(), range_types.find(maliput::SpeedLimitRuleTypeId()));

  // Verify YAML-sourced rule exists for seg1_lane1.
  const maliput::api::rules::Rule::Id yaml_rule(maliput::SpeedLimitRuleTypeId().string() + "/seg1_lane1_0");
  auto result = rulebook.GetRangeValueRule(yaml_rule);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(1u, result->states().size());
  EXPECT_DOUBLE_EQ(0.0, result->states()[0].min);
  EXPECT_DOUBLE_EQ(16.67, result->states()[0].max);
  EXPECT_EQ("Highway. [m/s]", result->states()[0].description);
}

// Verifies speed limits are merged from both YAML and GPKG sources.
TEST_F(RoadNetworkBuilderTest, SpeedLimitsFromBothSources) {
  // two_lane_road.gpkg has speed_limits for lane_1 (13.89) and lane_2 (8.33).
  // two_lane_road_extra_rules.yaml adds a YAML-sourced speed limit for lane_1 (5.55).
  const std::string kYamlFile{std::string(TEST_RESOURCES_DIR) + "/two_lane_road_extra_rules.yaml"};

  const std::map<std::string, std::string> config{
      {"gpkg_file", kGpkgFile},
      {"rule_registry", kYamlFile},
      {"road_rule_book", kYamlFile},
  };
  RoadNetworkBuilder builder(config);
  auto road_network = builder();

  ASSERT_NE(nullptr, road_network);

  const auto& rulebook = *road_network->rulebook();
  const auto& registry = *road_network->rule_registry();

  // Speed-Limit Rule Type must be registered (from YAML in this case).
  const auto& range_types = registry.RangeValueRuleTypes();
  ASSERT_NE(range_types.end(), range_types.find(maliput::SpeedLimitRuleTypeId()));

  // Verify YAML-sourced rule for lane_1 exists (5.55 m/s residential).
  const maliput::api::rules::Rule::Id yaml_rule("Speed-Limit Rule Type/yaml_lane_1_0");
  auto yaml_result = rulebook.GetRangeValueRule(yaml_rule);
  ASSERT_TRUE(yaml_result.has_value());
  ASSERT_EQ(1u, yaml_result->states().size());
  EXPECT_DOUBLE_EQ(0.0, yaml_result->states()[0].min);
  EXPECT_DOUBLE_EQ(5.55, yaml_result->states()[0].max);
  EXPECT_EQ("Residential zone. [m/s]", yaml_result->states()[0].description);

  // Verify GPKG-sourced rule for lane_1 also exists (13.89 m/s urban).
  const maliput::api::rules::Rule::Id gpkg_rule_1(maliput::SpeedLimitRuleTypeId().string() + "/lane_1_0");
  auto gpkg_result_1 = rulebook.GetRangeValueRule(gpkg_rule_1);
  ASSERT_TRUE(gpkg_result_1.has_value());
  ASSERT_EQ(1u, gpkg_result_1->states().size());
  EXPECT_DOUBLE_EQ(13.89, gpkg_result_1->states()[0].max);

  // Verify GPKG-sourced rule for lane_2 also exists (8.33 m/s school zone).
  const maliput::api::rules::Rule::Id gpkg_rule_2(maliput::SpeedLimitRuleTypeId().string() + "/lane_2_0");
  auto gpkg_result_2 = rulebook.GetRangeValueRule(gpkg_rule_2);
  ASSERT_TRUE(gpkg_result_2.has_value());
  ASSERT_EQ(1u, gpkg_result_2->states().size());
  EXPECT_DOUBLE_EQ(8.33, gpkg_result_2->states()[0].max);

  // Total: 3 speed limit RangeValueRules (1 from YAML + 2 from GPKG).
  // Verify by querying lane_1 which should have 2 rules (YAML + GPKG).
  const auto* lane = road_network->road_geometry()->ById().GetLane(maliput::api::LaneId("lane_1"));
  ASSERT_NE(nullptr, lane);
  const maliput::api::LaneSRange lane_s_range(lane->id(), maliput::api::SRange(0., lane->length()));
  const auto query_result = rulebook.FindRules({lane_s_range}, 0.);

  int lane_1_range_rules = 0;
  for (const auto& [id, rule] : query_result.range_value_rules) {
    if (id.string().find("lane_1") != std::string::npos) {
      ++lane_1_range_rules;
    }
  }
  // Expect 2 rules for lane_1: one from YAML (5.55) and one from GPKG (13.89).
  EXPECT_EQ(2, lane_1_range_rules);
}

}  // namespace
}  // namespace test
}  // namespace builder
}  // namespace maliput_geopackage
