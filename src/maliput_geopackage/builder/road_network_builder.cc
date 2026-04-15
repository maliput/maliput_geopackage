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

#include <memory>
#include <string>

#include <maliput/base/intersection_book.h>
#include <maliput/base/intersection_book_loader.h>
#include <maliput/base/manual_phase_provider.h>
#include <maliput/base/manual_phase_ring_book.h>
#include <maliput/base/manual_range_value_rule_state_provider.h>
#include <maliput/base/phase_based_right_of_way_rule_state_provider.h>
#include <maliput/base/phase_ring_book_loader.h>
#include <maliput/base/phased_discrete_rule_state_provider.h>
#include <maliput/base/road_object_book.h>
#include <maliput/base/traffic_light_book.h>
#include <maliput/base/traffic_light_book_loader.h>
#include <maliput/base/traffic_sign_book.h>
#include <maliput/common/logger.h>
#include <maliput_sparse/loader/road_geometry_loader.h>

#include "maliput_geopackage/builder/builder_configuration.h"
#include "maliput_geopackage/builder/road_rulebook_builder.h"
#include "maliput_geopackage/builder/rule_registry_builder.h"
#include "maliput_geopackage/geopackage/geopackage_manager.h"

namespace maliput_geopackage {
namespace builder {

std::unique_ptr<maliput::api::RoadNetwork> RoadNetworkBuilder::operator()() const {
  const BuilderConfiguration builder_config{BuilderConfiguration::FromMap(builder_config_)};

  maliput::log()->info("Loading GeoPackage from file: ", builder_config.gpkg_file, " ...");

  auto gpkg_manager = std::make_unique<geopackage::GeoPackageManager>(builder_config.gpkg_file);

  // Extract speed limits before moving the manager to the geometry loader.
  const auto speed_limits = gpkg_manager->GetSpeedLimits();

  maliput::log()->trace("Building RoadGeometry...");
  auto rg = maliput_sparse::loader::RoadGeometryLoader(std::move(gpkg_manager), builder_config.sparse_config)();

  maliput::log()->trace("Building TrafficLightBook...");
  auto traffic_light_book =
      !builder_config.sparse_config.traffic_light_book.has_value()
          ? std::make_unique<maliput::TrafficLightBook>()
          : maliput::LoadTrafficLightBookFromFile(builder_config.sparse_config.traffic_light_book.value());

  maliput::log()->trace("Building RuleRegistry...");
  auto rule_registry = RuleRegistryBuilder(speed_limits, builder_config.sparse_config.rule_registry)();

  maliput::log()->trace("Building RoadRulebook...");
  auto rule_book =
      RoadRuleBookBuilder(rg.get(), rule_registry.get(), speed_limits, builder_config.sparse_config.road_rule_book)();

  maliput::log()->trace("Building PhaseRingBook...");
  auto phase_ring_book = builder_config.sparse_config.phase_ring_book.has_value()
                             ? maliput::LoadPhaseRingBookFromFile(rule_book.get(), traffic_light_book.get(),
                                                                  builder_config.sparse_config.phase_ring_book.value())
                             : std::make_unique<maliput::ManualPhaseRingBook>();

  maliput::log()->trace("Building PhaseProvider...");
  auto phase_provider = maliput::ManualPhaseProvider::GetDefaultPopulatedManualPhaseProvider(phase_ring_book.get());

  maliput::log()->trace("Building DiscreteValueRuleStateProvider...");
  auto discrete_value_rule_state_provider =
      maliput::PhasedDiscreteRuleStateProvider::GetDefaultPhasedDiscreteRuleStateProvider(
          rule_book.get(), phase_ring_book.get(), phase_provider.get());

  maliput::log()->trace("Building RangeValueRuleStateProvider...");
  auto range_value_rule_state_provider =
      maliput::ManualRangeValueRuleStateProvider::GetDefaultManualRangeValueRuleStateProvider(rule_book.get());

  maliput::log()->trace("Building IntersectionBook...");
  auto intersection_book =
      !builder_config.sparse_config.intersection_book.has_value()
          ? std::make_unique<maliput::IntersectionBook>(rg.get())
          : maliput::LoadIntersectionBookFromFile(builder_config.sparse_config.intersection_book.value(), *rule_book,
                                                  *phase_ring_book, rg.get(), phase_provider.get());

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  auto state_provider =
      std::make_unique<maliput::PhaseBasedRightOfWayRuleStateProvider>(phase_ring_book.get(), phase_provider.get());
#pragma GCC diagnostic pop

  return std::make_unique<maliput::api::RoadNetwork>(
      std::move(rg), std::move(rule_book), std::move(traffic_light_book), std::move(intersection_book),
      std::move(phase_ring_book), std::move(state_provider), std::move(phase_provider), std::move(rule_registry),
      std::move(discrete_value_rule_state_provider), std::move(range_value_rule_state_provider),
      std::make_unique<maliput::RoadObjectBook>(), std::make_unique<maliput::TrafficSignBook>());
}

}  // namespace builder
}  // namespace maliput_geopackage
