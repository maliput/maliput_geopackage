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
#include "maliput_geopackage/geopackage/geopackage_manager.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include <maliput/api/lane_marking.h>
#include <maliput/common/logger.h>

#include "maliput_sparse/geometry/line_string.h"
#include "maliput_sparse/parser/lane.h"
#include "maliput_sparse/parser/segment.h"

namespace maliput_geopackage {
namespace geopackage {

using maliput_sparse::parser::Connection;
using maliput_sparse::parser::Junction;
using maliput_sparse::parser::Lane;
using maliput_sparse::parser::LaneEnd;
using maliput_sparse::parser::Segment;

/// @brief Convert GeoPackage lane_type string to maliput::api::LaneType enum.
/// @param lane_type_string The string value from the GeoPackage database (e.g., "driving", "shoulder").
/// @return The corresponding maliput::api::LaneType, or std::nullopt if the string is unknown.
static std::optional<maliput::api::LaneType> StringToLaneType(const std::string& lane_type_string) {
  if (lane_type_string == "driving") {
    return maliput::api::LaneType::kDriving;
  } else if (lane_type_string == "shoulder") {
    return maliput::api::LaneType::kShoulder;
  } else if (lane_type_string == "parking") {
    return maliput::api::LaneType::kParking;
  } else if (lane_type_string == "biking") {
    return maliput::api::LaneType::kBiking;
  } else if (lane_type_string == "walking") {
    return maliput::api::LaneType::kWalking;
  } else if (lane_type_string == "bus") {
    return maliput::api::LaneType::kBus;
  } else if (lane_type_string == "taxi") {
    return maliput::api::LaneType::kTaxi;
  } else if (lane_type_string == "hov") {
    return maliput::api::LaneType::kHov;
  } else if (lane_type_string == "turn") {
    return maliput::api::LaneType::kTurn;
  } else if (lane_type_string == "emergency") {
    return maliput::api::LaneType::kEmergency;
  }
  // Unknown or unrecognized lane type; default to nullopt
  maliput::log()->warn("Unknown lane_type: '", lane_type_string, "'; will default to kDriving if used.");
  return std::nullopt;
}

/// @brief Convert GeoPackage marking_type string to maliput::api::LaneMarkingType enum.
static maliput::api::LaneMarkingType StringToMarkingType(const std::string& type_str) {
  static const std::map<std::string, maliput::api::LaneMarkingType> kMapping = {
      {"solid", maliput::api::LaneMarkingType::kSolid},
      {"dashed", maliput::api::LaneMarkingType::kBroken},
      {"double_solid", maliput::api::LaneMarkingType::kSolidSolid},
      {"broken", maliput::api::LaneMarkingType::kBroken},  // alias for dashed
      {"double_broken", maliput::api::LaneMarkingType::kBrokenBroken},
      {"solid_solid", maliput::api::LaneMarkingType::kSolidSolid},
      {"solid_broken", maliput::api::LaneMarkingType::kSolidBroken},
      {"broken_solid", maliput::api::LaneMarkingType::kBrokenSolid},
  };
  auto it = kMapping.find(type_str);
  if (it != kMapping.end()) {
    return it->second;
  }
  maliput::log()->warn("Unknown marking_type: '", type_str, "'; defaulting to kSolid");
  return maliput::api::LaneMarkingType::kSolid;
}

/// @brief Convert GeoPackage color string to maliput::api::LaneMarkingColor enum.
static maliput::api::LaneMarkingColor StringToMarkingColor(const std::string& color_str) {
  static const std::map<std::string, maliput::api::LaneMarkingColor> kMapping{
      {"white", maliput::api::LaneMarkingColor::kWhite},
      {"yellow", maliput::api::LaneMarkingColor::kYellow},
      {"red", maliput::api::LaneMarkingColor::kRed},
      {"blue", maliput::api::LaneMarkingColor::kBlue},
  };
  auto it = kMapping.find(color_str);
  if (it != kMapping.end()) {
    return it->second;
  }
  maliput::log()->warn("Unknown marking color: '", color_str, "'; defaulting to kWhite");
  return maliput::api::LaneMarkingColor::kWhite;
}

/// @brief Convert GeoPackage weight string to maliput::api::LaneMarkingWeight enum.
static maliput::api::LaneMarkingWeight StringToMarkingWeight(const std::string& weight_str) {
  static const std::map<std::string, maliput::api::LaneMarkingWeight> kMapping{
      {"standard", maliput::api::LaneMarkingWeight::kStandard},
      {"bold", maliput::api::LaneMarkingWeight::kBold},
  };
  auto it = kMapping.find(weight_str);
  if (it != kMapping.end()) {
    return it->second;
  }
  maliput::log()->warn("Unknown marking weight: '", weight_str, "'; defaulting to kStandard");
  return maliput::api::LaneMarkingWeight::kStandard;
}

/// @brief Convert GeoPackage lane_change_rule string to maliput::api::LaneChangePermission enum.
static maliput::api::LaneChangePermission StringToLaneChangePermission(const std::string& rule_str) {
  static const std::map<std::string, maliput::api::LaneChangePermission> kMapping{
      {"prohibited", maliput::api::LaneChangePermission::kProhibited},
      {"left_only", maliput::api::LaneChangePermission::kToLeft},
      {"right_only", maliput::api::LaneChangePermission::kToRight},
      {"allowed", maliput::api::LaneChangePermission::kAllowed},
  };
  auto it = kMapping.find(rule_str);
  if (it != kMapping.end()) {
    return it->second;
  }
  maliput::log()->warn("Unknown lane_change_rule: '", rule_str, "'; defaulting to kProhibited");
  return maliput::api::LaneChangePermission::kProhibited;
}

GeoPackageManager::GeoPackageManager(const std::string& gpkg_file_path) : parser_(gpkg_file_path) {
  maliput::log()->trace("Constructing GeoPackageManager with file: ", gpkg_file_path);
  const auto& gpkg_junctions = parser_.GetJunctions();
  const auto& gpkg_segments = parser_.GetSegments();
  const auto& gpkg_lanes = parser_.GetLanes();
  const auto& gpkg_boundaries = parser_.GetLaneBoundaries();
  const auto& gpkg_branch_points = parser_.GetBranchPointLanes();
  const auto& gpkg_adjacent_lanes = parser_.GetAdjacentLanes();
  marking_map_ = BuildMarkingMap();

  // Build Lanes (without segment/junction hierarchy yet)
  maliput::log()->trace("Building lanes from parsed GeoPackage data...");
  std::unordered_map<std::string, Lane> lanes;

  for (const auto& [lane_id, gpkg_lane] : gpkg_lanes) {
    // Geometry
    auto get_geometry = [&](const std::string& boundary_id, bool inverted) {
      auto it = gpkg_boundaries.find(boundary_id);
      if (it == gpkg_boundaries.end()) {
        throw std::runtime_error("Missing boundary: " + boundary_id);
      }
      std::vector<maliput::math::Vector3> points = it->second.geometry;
      if (inverted) {
        std::reverse(points.begin(), points.end());
      }
      return maliput_sparse::geometry::LineString3d(points);
    };

    auto left_boundary = get_geometry(gpkg_lane.left_boundary_id, gpkg_lane.left_boundary_inverted);
    auto right_boundary = get_geometry(gpkg_lane.right_boundary_id, gpkg_lane.right_boundary_inverted);

    // Adjacency
    std::optional<std::string> left_lane_id;
    std::optional<std::string> right_lane_id;

    auto adj_it = gpkg_adjacent_lanes.find(lane_id);
    if (adj_it != gpkg_adjacent_lanes.end()) {
      for (const auto& adj : adj_it->second) {
        if (adj.side == "left")
          left_lane_id = adj.adjacent_lane_id;
        else if (adj.side == "right")
          right_lane_id = adj.adjacent_lane_id;
      }
    }

    // Create Lane (empty pred/succ for now)
    const auto left_markings_it = marking_map_.find(gpkg_lane.left_boundary_id);
    const auto right_markings_it = marking_map_.find(gpkg_lane.right_boundary_id);
    lanes.emplace(
        lane_id, Lane{lane_id,
                      left_boundary,
                      right_boundary,
                      left_lane_id,
                      right_lane_id,
                      gpkg_lane.left_boundary_id,
                      gpkg_lane.right_boundary_id,
                      StringToLaneType(gpkg_lane.lane_type),
                      left_markings_it != marking_map_.end() ? left_markings_it->second
                                                             : std::vector<maliput_sparse::parser::BoundaryMarkings>{},
                      right_markings_it != marking_map_.end() ? right_markings_it->second
                                                              : std::vector<maliput_sparse::parser::BoundaryMarkings>{},
                      {},
                      {}});
  }

  // Topology (Branch Points)
  maliput::log()->trace("Building topology from parsed GeoPackage data...");
  for (const auto& [bp_id, bp_lanes] : gpkg_branch_points) {
    std::vector<GPKGBranchPointLane> side_a;
    std::vector<GPKGBranchPointLane> side_b;

    for (const auto& bpl : bp_lanes) {
      if (bpl.side == "a")
        side_a.push_back(bpl);
      else if (bpl.side == "b")
        side_b.push_back(bpl);
    }

    // Connect A <-> B
    for (const auto& la : side_a) {
      for (const auto& lb : side_b) {
        LaneEnd::Which end_a = StrToLaneEndWhich(la.lane_end);
        LaneEnd::Which end_b = StrToLaneEndWhich(lb.lane_end);

        // Update Lane A
        if (end_a == LaneEnd::Which::kStart) {
          lanes.at(la.lane_id).predecessors.emplace(lb.lane_id, LaneEnd{lb.lane_id, end_b});
        } else {
          lanes.at(la.lane_id).successors.emplace(lb.lane_id, LaneEnd{lb.lane_id, end_b});
        }

        // Update Lane B
        if (end_b == LaneEnd::Which::kStart) {
          lanes.at(lb.lane_id).predecessors.emplace(la.lane_id, LaneEnd{la.lane_id, end_a});
        } else {
          lanes.at(lb.lane_id).successors.emplace(la.lane_id, LaneEnd{la.lane_id, end_a});
        }
      }
    }
  }

  // Build Segments and Junctions
  maliput::log()->trace("Building segments and junctions from parsed GeoPackage data...");
  std::unordered_map<std::string, std::vector<Lane>> segment_lanes;
  for (auto& [id, lane] : lanes) {
    std::string segment_id = gpkg_lanes.at(id).segment_id;
    segment_lanes[segment_id].push_back(std::move(lane));
  }

  for (auto& [segment_id, lanes_in_segment] : segment_lanes) {
    SortLanes(lanes_in_segment);
  }

  std::unordered_map<std::string, std::vector<Segment>> junction_segments;
  for (const auto& [segment_id, gpkg_segment] : gpkg_segments) {
    Segment segment;
    segment.id = segment_id;
    segment.lanes = std::move(segment_lanes[segment_id]);

    junction_segments[gpkg_segment.junction_id].push_back(std::move(segment));
  }

  for (const auto& [junction_id, gpkg_junction] : gpkg_junctions) {
    Junction junction;
    junction.id = junction_id;
    for (auto& seg : junction_segments[junction_id]) {
      junction.segments.emplace(seg.id, std::move(seg));
    }
    junctions_.emplace(junction_id, std::move(junction));
  }

  // Populate Connections vector
  maliput::log()->trace("Building connections from parsed GeoPackage data...");
  for (const auto& [jid, junction] : junctions_) {
    for (const auto& [sid, segment] : junction.segments) {
      for (const auto& lane : segment.lanes) {
        for (const auto& [pred_id, pred_end] : lane.predecessors) {
          connections_.push_back({pred_end, {lane.id, LaneEnd::Which::kStart}});
        }
        for (const auto& [succ_id, succ_end] : lane.successors) {
          connections_.push_back({{lane.id, LaneEnd::Which::kFinish}, succ_end});
        }
      }
    }
  }

  // Deduplicate connections (treat A<->B as identical regardless of ordering).
  maliput::log()->trace("Deduplicating connections");
  if (!connections_.empty()) {
    auto canonical_key = [](const Connection& c) {
      // Normalize lane ends so that the pair's ordering is deterministic.
      const auto make_pair = [](const LaneEnd& le) { return std::make_pair(le.lane_id, static_cast<int>(le.end)); };
      const auto a = make_pair(c.from);
      const auto b = make_pair(c.to);
      return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
    };

    std::set<decltype(canonical_key(connections_[0]))> seen;
    std::vector<Connection> unique_connections;
    unique_connections.reserve(connections_.size());

    for (const auto& c : connections_) {
      const auto key = canonical_key(c);
      if (seen.insert(key).second) {
        unique_connections.push_back(c);
      }
    }
    connections_.swap(unique_connections);
  }

  maliput::log()->trace("Loaded lane marking map with ", std::to_string(marking_map_.size()), " boundaries.");
}

GeoPackageManager::~GeoPackageManager() = default;

const std::unordered_map<std::string, std::vector<GPKGSpeedLimit>>& GeoPackageManager::GetSpeedLimits() const {
  return parser_.GetSpeedLimits();
}

const std::unordered_map<std::string, std::vector<maliput_sparse::parser::BoundaryMarkings>>&
GeoPackageManager::GetMarkings() const {
  return marking_map_;
}

const std::unordered_map<maliput_sparse::parser::Junction::Id, maliput_sparse::parser::Junction>&
GeoPackageManager::DoGetJunctions() const {
  return junctions_;
}

const std::vector<maliput_sparse::parser::Connection>& GeoPackageManager::DoGetConnections() const {
  return connections_;
}

const std::string& GeoPackageManager::DoGetGeoReferenceInfo() const {
  static const std::string empty_geo_ref = "";
  auto it = parser_.GetMetadata().find("geo_reference_info");
  if (it != parser_.GetMetadata().end()) {
    return it->second;
  }
  return empty_geo_ref;
}

LaneEnd::Which GeoPackageManager::StrToLaneEndWhich(const std::string& s) const {
  if (s == "start") return LaneEnd::Which::kStart;
  if (s == "finish") return LaneEnd::Which::kFinish;
  throw std::runtime_error("Invalid lane end: " + s);
}

void GeoPackageManager::SortLanes(std::vector<Lane>& lanes) const {
  if (lanes.empty()) return;

  std::unordered_map<std::string, size_t> id_to_index;
  for (size_t i = 0; i < lanes.size(); ++i) {
    id_to_index[lanes[i].id] = i;
  }

  // Find start candidates (lanes with no right neighbor in the segment).
  std::vector<size_t> start_indices;
  for (size_t i = 0; i < lanes.size(); ++i) {
    const auto& lane = lanes[i];
    bool right_neighbor_in_segment = false;
    if (lane.right_lane_id.has_value()) {
      if (id_to_index.find(lane.right_lane_id.value()) != id_to_index.end()) {
        right_neighbor_in_segment = true;
      }
    }
    if (!right_neighbor_in_segment) {
      start_indices.push_back(i);
    }
  }

  // If no start found (e.g. pure cycle), just pick the first one to break it somewhere
  if (start_indices.empty() && !lanes.empty()) {
    start_indices.push_back(0);
  }

  std::vector<Lane> sorted_lanes;
  sorted_lanes.reserve(lanes.size());
  std::vector<bool> moved(lanes.size(), false);

  for (size_t start_idx : start_indices) {
    size_t current_idx = start_idx;
    while (true) {
      if (moved[current_idx]) break;

      // Move lane to sorted list
      moved[current_idx] = true;
      sorted_lanes.push_back(std::move(lanes[current_idx]));

      // Find next lane to the left
      const auto& current_lane = sorted_lanes.back();
      if (current_lane.left_lane_id.has_value()) {
        auto it = id_to_index.find(current_lane.left_lane_id.value());
        if (it != id_to_index.end()) {
          current_idx = it->second;
        } else {
          break;  // Left neighbor not in segment
        }
      } else {
        break;  // No left neighbor
      }
    }
  }

  // Append any remaining lanes (e.g. unreachable from starts due to cycles or other issues)
  for (size_t i = 0; i < lanes.size(); ++i) {
    if (!moved[i]) {
      sorted_lanes.push_back(std::move(lanes[i]));
    }
  }

  lanes = std::move(sorted_lanes);
}

std::unordered_map<std::string, std::vector<maliput_sparse::parser::BoundaryMarkings>>
GeoPackageManager::BuildMarkingMap() const {
  std::unordered_map<std::string, std::vector<maliput_sparse::parser::BoundaryMarkings>> result;

  const auto& gpkg_markings = parser_.GetMarkings();

  for (const auto& [boundary_id, markings] : gpkg_markings) {
    for (const auto& gpkg_marking : markings) {
      maliput_sparse::parser::LaneMarking marking;
      marking.type = StringToMarkingType(gpkg_marking.marking_type);
      marking.color = StringToMarkingColor(gpkg_marking.color);
      marking.weight = StringToMarkingWeight(gpkg_marking.weight);
      marking.width = gpkg_marking.width;
      marking.height = gpkg_marking.height;
      marking.material = gpkg_marking.material;
      marking.lane_change = StringToLaneChangePermission(gpkg_marking.lane_change_rule);

      // Convert line details
      for (const auto& gpkg_line : gpkg_marking.lines) {
        maliput_sparse::parser::LaneMarkingLine line;
        line.length = gpkg_line.length;
        line.space = gpkg_line.space;
        line.width = gpkg_line.width;
        line.r_offset = gpkg_line.r_offset;
        line.color = StringToMarkingColor(gpkg_line.color);
        marking.lines.push_back(line);
      }

      result[boundary_id].push_back({gpkg_marking.s_start, gpkg_marking.s_end, marking});
    }
  }

  return result;
}

}  // namespace geopackage
}  // namespace maliput_geopackage
