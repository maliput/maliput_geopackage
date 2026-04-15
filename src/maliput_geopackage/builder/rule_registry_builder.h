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
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <maliput/base/rule_registry.h>
#include <maliput/common/maliput_copyable.h>

#include "maliput_geopackage/geopackage/geopackage_parser.h"

namespace maliput_geopackage {
namespace builder {

/// Functor to build a maliput::api::rules::RuleRegistry.
///
/// The building procedure is:
/// 1. If a YAML file path is provided, loads rule types from the file.
/// 2. If GeoPackage speed limits are provided and the Speed-Limit Rule Type
///    is not yet registered, registers it with the speed limit ranges extracted
///    from the GeoPackage data.
class RuleRegistryBuilder {
 public:
  MALIPUT_NO_COPY_NO_MOVE_NO_ASSIGN(RuleRegistryBuilder)

  /// Constructs a RuleRegistryBuilder.
  ///
  /// @param speed_limits Speed limit data from GeoPackage, keyed by lane ID.
  /// @param rule_registry_file_path Optional YAML file path for loading the rule registry.
  RuleRegistryBuilder(const std::unordered_map<std::string, std::vector<geopackage::GPKGSpeedLimit>>& speed_limits,
                      const std::optional<std::string>& rule_registry_file_path);

  /// Builds a maliput::api::rules::RuleRegistry.
  /// @return A RuleRegistry populated from YAML and/or GeoPackage data.
  std::unique_ptr<maliput::api::rules::RuleRegistry> operator()() const;

 private:
  const std::unordered_map<std::string, std::vector<geopackage::GPKGSpeedLimit>>& speed_limits_;
  const std::optional<std::string> rule_registry_file_path_;
};

}  // namespace builder
}  // namespace maliput_geopackage
