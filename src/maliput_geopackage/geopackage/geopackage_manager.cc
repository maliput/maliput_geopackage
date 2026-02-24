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
#include <optional>
#include <stdexcept>
#include <vector>

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

GeoPackageManager::GeoPackageManager(const std::string& gpkg_file_path) : parser_(gpkg_file_path) {
  const auto& gpkg_junctions = parser_.GetJunctions();
  const auto& gpkg_segments = parser_.GetSegments();
  const auto& gpkg_lanes = parser_.GetLanes();
  const auto& gpkg_boundaries = parser_.GetLaneBoundaries();
  const auto& gpkg_branch_points = parser_.GetBranchPointLanes();
  const auto& gpkg_adjacent_lanes = parser_.GetAdjacentLanes();

  // Build Lanes (without segment/junction hierarchy yet)
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
    lanes.emplace(lane_id, Lane{lane_id, left_boundary, right_boundary, left_lane_id, right_lane_id, {}, {}});
  }

  // Topology (Branch Points)
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
  std::unordered_map<std::string, std::vector<Lane>> segment_lanes;
  for (auto& [id, lane] : lanes) {
    std::string segment_id = gpkg_lanes.at(id).segment_id;
    segment_lanes[segment_id].push_back(std::move(lane));
  }

  for (auto& [segment_id, lanes_in_segment] : segment_lanes) {
    SortLanes(&lanes_in_segment);
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

  // Deduplicate connections
  std::sort(connections_.begin(), connections_.end(), [](const Connection& a, const Connection& b) {
    if (a.from.lane_id != b.from.lane_id) return a.from.lane_id < b.from.lane_id;
    if (a.from.end != b.from.end) return a.from.end < b.from.end;
    if (a.to.lane_id != b.to.lane_id) return a.to.lane_id < b.to.lane_id;
    return a.to.end < b.to.end;
  });
  connections_.erase(std::unique(connections_.begin(), connections_.end()), connections_.end());
}

GeoPackageManager::~GeoPackageManager() = default;

const std::unordered_map<maliput_sparse::parser::Junction::Id, maliput_sparse::parser::Junction>&
GeoPackageManager::DoGetJunctions() const {
  return junctions_;
}

const std::vector<maliput_sparse::parser::Connection>& GeoPackageManager::DoGetConnections() const {
  return connections_;
}

LaneEnd::Which GeoPackageManager::StrToLaneEndWhich(const std::string& s) const {
  if (s == "start") return LaneEnd::Which::kStart;
  if (s == "finish") return LaneEnd::Which::kFinish;
  throw std::runtime_error("Invalid lane end: " + s);
}

void GeoPackageManager::SortLanes(std::vector<Lane>* lanes) {
  if (lanes->empty()) return;

  std::unordered_map<std::string, size_t> id_to_index;
  for (size_t i = 0; i < lanes->size(); ++i) {
    id_to_index[(*lanes)[i].id] = i;
  }

  // Find start candidates (lanes with no right neighbor in the segment).
  std::vector<size_t> start_indices;
  for (size_t i = 0; i < lanes->size(); ++i) {
    const auto& lane = (*lanes)[i];
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

  // If no start found (e.g. pure cycle), just pick the first one to break it somewhere?
  // Or keep original order.
  if (start_indices.empty() && !lanes->empty()) {
    start_indices.push_back(0);
  }

  std::vector<Lane> sorted_lanes;
  sorted_lanes.reserve(lanes->size());
  std::vector<bool> moved(lanes->size(), false);

  for (size_t start_idx : start_indices) {
    size_t current_idx = start_idx;
    while (true) {
      if (moved[current_idx]) break;

      // Move lane to sorted list
      moved[current_idx] = true;
      sorted_lanes.push_back(std::move((*lanes)[current_idx]));

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
  for (size_t i = 0; i < lanes->size(); ++i) {
    if (!moved[i]) {
      sorted_lanes.push_back(std::move((*lanes)[i]));
    }
  }

  *lanes = std::move(sorted_lanes);
}

}  // namespace geopackage
}  // namespace maliput_geopackage
