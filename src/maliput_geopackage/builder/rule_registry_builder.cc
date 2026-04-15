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
#include "maliput_geopackage/builder/rule_registry_builder.h"

#include <set>

#include <maliput/api/rules/range_value_rule.h>
#include <maliput/api/rules/rule.h>
#include <maliput/base/rule_registry.h>
#include <maliput/base/rule_registry_loader.h>
#include <maliput/common/logger.h>

namespace maliput_geopackage {
namespace builder {

using maliput::api::rules::RangeValueRule;
using maliput::api::rules::Rule;

RuleRegistryBuilder::RuleRegistryBuilder(
    const std::unordered_map<std::string, std::vector<geopackage::GPKGSpeedLimit>>& speed_limits,
    const std::optional<std::string>& rule_registry_file_path)
    : speed_limits_(speed_limits), rule_registry_file_path_(rule_registry_file_path) {}

std::unique_ptr<maliput::api::rules::RuleRegistry> RuleRegistryBuilder::operator()() const {
  maliput::log()->trace(rule_registry_file_path_.has_value()
                            ? "RuleRegistry file provided: " + rule_registry_file_path_.value()
                            : "No RuleRegistry file provided");

  auto rule_registry = !rule_registry_file_path_.has_value()
                           ? std::make_unique<maliput::api::rules::RuleRegistry>()
                           : maliput::LoadRuleRegistryFromFile(rule_registry_file_path_.value());

  // Register Speed-Limit Rule Type from GeoPackage data if not already registered.
  if (!speed_limits_.empty()) {
    const auto existing = rule_registry->GetPossibleStatesOfRuleType(maliput::SpeedLimitRuleTypeId());
    if (!existing.has_value()) {
      maliput::log()->trace("Registering Speed-Limit Rule Type from GeoPackage data...");
      std::set<RangeValueRule::Range> unique_ranges;
      for (const auto& [lane_id, lane_speed_limits] : speed_limits_) {
        for (const auto& sl : lane_speed_limits) {
          unique_ranges.emplace(sl.severity, Rule::RelatedRules{}, Rule::RelatedUniqueIds{}, sl.description,
                                sl.min_speed, sl.max_speed);
        }
      }
      rule_registry->RegisterRangeValueRule(
          maliput::SpeedLimitRuleTypeId(),
          std::vector<RangeValueRule::Range>(unique_ranges.begin(), unique_ranges.end()));
    } else {
      maliput::log()->trace("Speed-Limit Rule Type already registered from YAML, skipping GeoPackage registration.");
    }
  }

  return rule_registry;
}

}  // namespace builder
}  // namespace maliput_geopackage
