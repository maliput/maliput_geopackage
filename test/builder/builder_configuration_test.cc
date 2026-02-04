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

#include "maliput_geopackage/builder/builder_configuration.h"

#include <map>
#include <string>

#include <gtest/gtest.h>

#include "maliput_geopackage/builder/params.h"

namespace maliput_geopackage {
namespace builder {
namespace test {
namespace {

class BuilderConfigurationTest : public ::testing::Test {
 protected:
  const std::string kGpkgFile{"/path/to/road_network.gpkg"};
  const std::string kRoadGeometryId{"my_road_geometry"};
  const std::string kLinearTolerance{"0.01"};
  const std::string kAngularTolerance{"0.02"};
  const std::string kScaleLength{"2.0"};
  const std::string kInertialToBackendFrameTranslation{"{1., 2., 3.}"};
  const std::string kRoadRuleBook{"/path/to/road_rule_book.yaml"};
  const std::string kRuleRegistry{"/path/to/rule_registry.yaml"};
  const std::string kTrafficLightBook{"/path/to/traffic_light_book.yaml"};
  const std::string kPhaseRingBook{"/path/to/phase_ring_book.yaml"};
  const std::string kIntersectionBook{"/path/to/intersection_book.yaml"};
};

TEST_F(BuilderConfigurationTest, DefaultConstructor) {
  const BuilderConfiguration dut{};

  EXPECT_TRUE(dut.gpkg_file.empty());
  // Verify sparse_config defaults are applied
  EXPECT_EQ("maliput_sparse", dut.sparse_config.road_geometry_id.string());
  EXPECT_DOUBLE_EQ(1.e-3, dut.sparse_config.linear_tolerance);
  EXPECT_DOUBLE_EQ(1.e-3, dut.sparse_config.angular_tolerance);
  EXPECT_DOUBLE_EQ(1., dut.sparse_config.scale_length);
  EXPECT_EQ(maliput::math::Vector3(0., 0., 0.), dut.sparse_config.inertial_to_backend_frame_translation);
  EXPECT_FALSE(dut.sparse_config.rule_registry.has_value());
  EXPECT_FALSE(dut.sparse_config.road_rule_book.has_value());
  EXPECT_FALSE(dut.sparse_config.traffic_light_book.has_value());
  EXPECT_FALSE(dut.sparse_config.phase_ring_book.has_value());
  EXPECT_FALSE(dut.sparse_config.intersection_book.has_value());
}

TEST_F(BuilderConfigurationTest, FromMapWithAllParameters) {
  const std::map<std::string, std::string> config{
      {params::kGpkgFile, kGpkgFile},
      {params::kRoadGeometryId, kRoadGeometryId},
      {params::kLinearTolerance, kLinearTolerance},
      {params::kAngularTolerance, kAngularTolerance},
      {params::kScaleLength, kScaleLength},
      {params::kInertialToBackendFrameTranslation, kInertialToBackendFrameTranslation},
      {params::kRoadRuleBook, kRoadRuleBook},
      {params::kRuleRegistry, kRuleRegistry},
      {params::kTrafficLightBook, kTrafficLightBook},
      {params::kPhaseRingBook, kPhaseRingBook},
      {params::kIntersectionBook, kIntersectionBook},
  };

  const BuilderConfiguration dut = BuilderConfiguration::FromMap(config);

  EXPECT_EQ(kGpkgFile, dut.gpkg_file);
  EXPECT_EQ(kRoadGeometryId, dut.sparse_config.road_geometry_id.string());
  EXPECT_DOUBLE_EQ(0.01, dut.sparse_config.linear_tolerance);
  EXPECT_DOUBLE_EQ(0.02, dut.sparse_config.angular_tolerance);
  EXPECT_DOUBLE_EQ(2.0, dut.sparse_config.scale_length);
  EXPECT_EQ(maliput::math::Vector3(1., 2., 3.), dut.sparse_config.inertial_to_backend_frame_translation);
  ASSERT_TRUE(dut.sparse_config.road_rule_book.has_value());
  EXPECT_EQ(kRoadRuleBook, dut.sparse_config.road_rule_book.value());
  ASSERT_TRUE(dut.sparse_config.rule_registry.has_value());
  EXPECT_EQ(kRuleRegistry, dut.sparse_config.rule_registry.value());
  ASSERT_TRUE(dut.sparse_config.traffic_light_book.has_value());
  EXPECT_EQ(kTrafficLightBook, dut.sparse_config.traffic_light_book.value());
  ASSERT_TRUE(dut.sparse_config.phase_ring_book.has_value());
  EXPECT_EQ(kPhaseRingBook, dut.sparse_config.phase_ring_book.value());
  ASSERT_TRUE(dut.sparse_config.intersection_book.has_value());
  EXPECT_EQ(kIntersectionBook, dut.sparse_config.intersection_book.value());
}

TEST_F(BuilderConfigurationTest, FromMapWithOnlyGpkgFile) {
  const std::map<std::string, std::string> config{
      {params::kGpkgFile, kGpkgFile},
  };

  const BuilderConfiguration dut = BuilderConfiguration::FromMap(config);

  EXPECT_EQ(kGpkgFile, dut.gpkg_file);
  // Verify defaults are still applied for sparse_config
  EXPECT_EQ("maliput_sparse", dut.sparse_config.road_geometry_id.string());
  EXPECT_DOUBLE_EQ(1.e-3, dut.sparse_config.linear_tolerance);
  EXPECT_DOUBLE_EQ(1.e-3, dut.sparse_config.angular_tolerance);
}

TEST_F(BuilderConfigurationTest, FromMapWithEmptyMap) {
  const std::map<std::string, std::string> config{};

  const BuilderConfiguration dut = BuilderConfiguration::FromMap(config);

  EXPECT_TRUE(dut.gpkg_file.empty());
  EXPECT_EQ("maliput_sparse", dut.sparse_config.road_geometry_id.string());
}

TEST_F(BuilderConfigurationTest, ToStringMapWithAllParameters) {
  BuilderConfiguration dut{};
  dut.gpkg_file = kGpkgFile;
  dut.sparse_config.road_geometry_id = maliput::api::RoadGeometryId{kRoadGeometryId};
  dut.sparse_config.linear_tolerance = 0.01;
  dut.sparse_config.angular_tolerance = 0.02;
  dut.sparse_config.scale_length = 2.0;
  dut.sparse_config.inertial_to_backend_frame_translation = maliput::math::Vector3{1., 2., 3.};
  dut.sparse_config.road_rule_book = kRoadRuleBook;
  dut.sparse_config.rule_registry = kRuleRegistry;
  dut.sparse_config.traffic_light_book = kTrafficLightBook;
  dut.sparse_config.phase_ring_book = kPhaseRingBook;
  dut.sparse_config.intersection_book = kIntersectionBook;

  const std::map<std::string, std::string> result = dut.ToStringMap();

  EXPECT_EQ(kGpkgFile, result.at(params::kGpkgFile));
  EXPECT_EQ(kRoadGeometryId, result.at(params::kRoadGeometryId));
  // Note: ToStringMap uses std::to_string which may add trailing zeros
  EXPECT_DOUBLE_EQ(0.01, std::stod(result.at(params::kLinearTolerance)));
  EXPECT_DOUBLE_EQ(0.02, std::stod(result.at(params::kAngularTolerance)));
  EXPECT_DOUBLE_EQ(2.0, std::stod(result.at(params::kScaleLength)));
  EXPECT_EQ(kRoadRuleBook, result.at(params::kRoadRuleBook));
  EXPECT_EQ(kRuleRegistry, result.at(params::kRuleRegistry));
  EXPECT_EQ(kTrafficLightBook, result.at(params::kTrafficLightBook));
  EXPECT_EQ(kPhaseRingBook, result.at(params::kPhaseRingBook));
  EXPECT_EQ(kIntersectionBook, result.at(params::kIntersectionBook));
}

TEST_F(BuilderConfigurationTest, ToStringMapWithDefaults) {
  const BuilderConfiguration dut{};

  const std::map<std::string, std::string> result = dut.ToStringMap();

  EXPECT_TRUE(result.at(params::kGpkgFile).empty());
  EXPECT_EQ("maliput_sparse", result.at(params::kRoadGeometryId));
}

TEST_F(BuilderConfigurationTest, RoundTripFromMapToStringMap) {
  const std::map<std::string, std::string> original_config{
      {params::kGpkgFile, kGpkgFile},
      {params::kRoadGeometryId, kRoadGeometryId},
      {params::kLinearTolerance, kLinearTolerance},
      {params::kAngularTolerance, kAngularTolerance},
  };

  const BuilderConfiguration dut = BuilderConfiguration::FromMap(original_config);
  const std::map<std::string, std::string> result = dut.ToStringMap();

  EXPECT_EQ(kGpkgFile, result.at(params::kGpkgFile));
  EXPECT_EQ(kRoadGeometryId, result.at(params::kRoadGeometryId));
  // Note: ToStringMap uses std::to_string which may change floating point formatting
  EXPECT_DOUBLE_EQ(std::stod(kLinearTolerance), std::stod(result.at(params::kLinearTolerance)));
  EXPECT_DOUBLE_EQ(std::stod(kAngularTolerance), std::stod(result.at(params::kAngularTolerance)));
}

}  // namespace
}  // namespace test
}  // namespace builder
}  // namespace maliput_geopackage
