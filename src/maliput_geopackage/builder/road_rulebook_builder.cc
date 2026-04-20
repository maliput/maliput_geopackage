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
#include "maliput_geopackage/builder/road_rulebook_builder.h"

#include <string>

#include <maliput/api/regions.h>
#include <maliput/api/rules/range_value_rule.h>
#include <maliput/api/rules/speed_limit_rule.h>
#include <maliput/base/road_rulebook_loader.h>
#include <maliput/common/logger.h>
#include <maliput/common/maliput_throw.h>

namespace maliput_geopackage {
namespace builder {

using maliput::api::LaneSRange;
using maliput::api::LaneSRoute;
using maliput::api::SRange;
using maliput::api::rules::RangeValueRule;
using maliput::api::rules::Rule;

RoadRuleBookBuilder::RoadRuleBookBuilder(
    const maliput::api::RoadGeometry* rg, const maliput::api::rules::RuleRegistry* rule_registry,
    const std::unordered_map<std::string, std::vector<geopackage::GPKGSpeedLimit>>& speed_limits,
    const std::optional<std::string>& road_rulebook_file_path)
    : rg_(rg),
      rule_registry_(rule_registry),
      speed_limits_(speed_limits),
      road_rulebook_file_path_(road_rulebook_file_path) {
  MALIPUT_THROW_UNLESS(rg_ != nullptr);
  MALIPUT_THROW_UNLESS(rule_registry_ != nullptr);
}

std::unique_ptr<maliput::ManualRulebook> RoadRuleBookBuilder::operator()() const {
  maliput::log()->trace(road_rulebook_file_path_.has_value()
                            ? "RoadRulebook file provided: " + road_rulebook_file_path_.value()
                            : "No RoadRulebook file provided");

  auto rulebook = road_rulebook_file_path_.has_value()
                      ? maliput::LoadRoadRulebookFromFile(rg_, road_rulebook_file_path_.value(), *rule_registry_)
                      : std::make_unique<maliput::ManualRulebook>();

  auto* manual_rulebook = dynamic_cast<maliput::ManualRulebook*>(rulebook.get());
  MALIPUT_THROW_UNLESS(manual_rulebook != nullptr);

  AddGpkgSpeedLimitRules(manual_rulebook);

  // Release the base pointer and return the ManualRulebook directly.
  rulebook.release();
  return std::unique_ptr<maliput::ManualRulebook>(manual_rulebook);
}

void RoadRuleBookBuilder::AddGpkgSpeedLimitRules(maliput::ManualRulebook* rulebook) const {
  if (speed_limits_.empty()) {
    return;
  }
  maliput::log()->trace("Adding GeoPackage speed limit rules to rulebook...");

  int total_rules = 0;
  for (const auto& [lane_id, lane_speed_limits] : speed_limits_) {
    int lane_rule_index = 0;
    for (const auto& sl : lane_speed_limits) {
      const LaneSRange lane_s_range(maliput::api::LaneId(lane_id), SRange(sl.s_start, sl.s_end));
      const Rule::Id rule_id(maliput::SpeedLimitRuleTypeId().string() + "/" + lane_id + "_" +
                             std::to_string(lane_rule_index));

      // New API: RangeValueRule.
      const LaneSRoute zone({lane_s_range});
      const RangeValueRule::Range range(sl.severity, Rule::RelatedRules{}, Rule::RelatedUniqueIds{}, sl.description,
                                        sl.min_speed, sl.max_speed);
      rulebook->AddRule(maliput::api::rules::RangeValueRule(rule_id, maliput::SpeedLimitRuleTypeId(), zone, {range}));

      // Deprecated API: SpeedLimitRule (for backward compatibility with maliput_query GetMaxSpeedLimit).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      rulebook->AddRule(maliput::api::rules::SpeedLimitRule(
          maliput::api::rules::SpeedLimitRule::Id(rule_id.string()), lane_s_range,
          static_cast<maliput::api::rules::SpeedLimitRule::Severity>(sl.severity), sl.min_speed, sl.max_speed));
#pragma GCC diagnostic pop

      ++lane_rule_index;
      ++total_rules;
    }
  }
  maliput::log()->trace("Added ", total_rules, " speed limit rules from GeoPackage.");
}

}  // namespace builder
}  // namespace maliput_geopackage
