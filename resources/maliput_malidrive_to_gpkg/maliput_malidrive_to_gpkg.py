#!/usr/bin/env python3
# BSD 3-Clause License
#
# Copyright (c) 2026, Woven by Toyota.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Migrate an OpenDRIVE (.xodr) road network to GeoPackage (.gpkg) format.

Loads any XODR map via the maliput Python API (maliput_malidrive backend),
samples lane boundaries from the road geometry, and writes the result as a .gpkg
file conforming to the maliput_geopackage schema.

Setup:
    uv venv -p 3.10
    uv pip install maliput maliput_malidrive

Usage:
    uv run python maliput_malidrive_to_gpkg.py <path/to/map.xodr> [-o output.gpkg]
    uv run python maliput_malidrive_to_gpkg.py /path/to/TShapeRoad.xodr
    uv run python maliput_malidrive_to_gpkg.py /path/to/Town01.xodr -o town01.gpkg
"""

import argparse
import heapq
import math
import shutil
import sqlite3
import struct
from pathlib import Path

import maliput
from maliput.api import LanePosition, Which

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_TEMPLATE_GPKG = SCRIPT_DIR / ".." / ".." / "schema" / "tools" / "template.gpkg"

# Sampling budget: maximum number of points per lane boundary.
# The adaptive sampler may use fewer points for straight geometry.
NUM_SAMPLES = 50

# Maximum allowed deviation in meters between the sampled polyline and the
# underlying boundary geometry before adaptive refinement is triggered.
MAX_CHORD_ERROR = 5e-2

# GeoPackage SRS ID for the local Cartesian coordinate system
SRS_ID = 100000

# Tolerances used when loading the XODR file
LINEAR_TOLERANCE = "5e-2"
ANGULAR_TOLERANCE = "1e-3"


# -----------------------------------------------------------------------------
# CLI argument parsing
# -----------------------------------------------------------------------------

def parse_args():
    """Parse command-line arguments.

    Returns:
        argparse.Namespace: Parsed CLI arguments.
    """
    parser = argparse.ArgumentParser(
        description="Convert an OpenDRIVE (.xodr) road network to GeoPackage (.gpkg) format.",
    )
    parser.add_argument(
        "xodr_file",
        help="Path to the input OpenDRIVE (.xodr) file.",
    )
    parser.add_argument(
        "-o", "--output",
        default=None,
        help="Path for the output .gpkg file. Defaults to <input_stem>.gpkg in the current directory.",
    )
    parser.add_argument(
        "--num-samples",
        type=int,
        default=NUM_SAMPLES,
        help=f"Maximum number of sample points per lane boundary (default: {NUM_SAMPLES}).",
    )
    parser.add_argument(
        "--max-chord-error",
        type=float,
        default=MAX_CHORD_ERROR,
        help=(
            "Maximum boundary polyline deviation in meters before adaptive refinement "
            f"adds more samples (default: {MAX_CHORD_ERROR})."
        ),
    )
    parser.add_argument(
        "--linear-tolerance",
        default=LINEAR_TOLERANCE,
        help=f"Linear tolerance in meters (default: {LINEAR_TOLERANCE}).",
    )
    parser.add_argument(
        "--angular-tolerance",
        default=ANGULAR_TOLERANCE,
        help=f"Angular tolerance in radians (default: {ANGULAR_TOLERANCE}).",
    )
    parser.add_argument(
        "--omit-nondrivable-lanes",
        action="store_true",
        default=True,
        help="Omit non-drivable lanes (sidewalks, shoulders, etc.).",
    )
    parser.add_argument(
        "--template",
        default=str(DEFAULT_TEMPLATE_GPKG),
        help=f"Path to the template .gpkg file (default: {DEFAULT_TEMPLATE_GPKG}).",
    )
    return parser.parse_args()


# -----------------------------------------------------------------------------
# GeoPackageBinary geometry encoding
# -----------------------------------------------------------------------------

def build_gpkg_linestring_z(points_3d, srs_id=SRS_ID):
    """Build a GeoPackageBinary BLOB for a 3D LINESTRING (with Z).

    Format: [GP header (8 bytes)] + [WKB LINESTRING Z]

    The WKB type uses the ISO SQL/MM flag: bit 31 set for Z → 0x80000002.
    This matches the parser in geopackage_parser.cc.

    Args:
        points_3d: Iterable of (x, y, z) tuples.
        srs_id: Spatial reference system ID.

    Returns:
        bytes: GeoPackageBinary BLOB.
    """
    pts = list(points_3d)
    n = len(pts)
    assert n >= 2, "LINESTRING requires at least 2 points"

    # GeoPackage header (8 bytes):
    #   magic "GP" (2 bytes) + version 0 (1 byte) + flags 0x01 (1 byte, LE, no envelope)
    #   + srs_id (4 bytes, LE int32)
    header = struct.pack("<2sBBi", b"GP", 0, 0x01, srs_id)

    # WKB LINESTRING Z:
    #   byte_order (1 byte, 0x01 = LE)
    #   + wkb_type (4 bytes, 0x80000002 = LINESTRING with Z)
    #   + num_points (4 bytes)
    #   + x,y,z doubles per point
    wkb_type = 0x80000002  # LINESTRING Z (ISO SQL/MM convention)
    wkb_header = struct.pack("<BII", 1, wkb_type, n)
    wkb_coords = b"".join(struct.pack("<ddd", x, y, z) for x, y, z in pts)

    return header + wkb_header + wkb_coords


# -----------------------------------------------------------------------------
# Lane boundary sampling
# -----------------------------------------------------------------------------

def boundary_r_at_s(lane, side, s):
    """Return the lateral boundary coordinate for a lane side at s.

    Args:
        lane: A maliput Lane object.
        side: Boundary side selector, either "left" or "right".
        s: Longitudinal lane coordinate in meters.

    Returns:
        float: Lateral r coordinate for the requested lane boundary.
    """
    bounds = lane.lane_bounds(s)
    return bounds.max_r() if side == "left" else bounds.min_r()


def sample_boundary_point(lane, side, s):
    """Sample a boundary point and its curvature at a given s position.

    Args:
        lane: A maliput Lane object.
        side: Boundary side selector, either "left" or "right".
        s: Longitudinal lane coordinate in meters.

    Returns:
        Tuple[Tuple[float, float, float], float]: A 3D point in inertial space
            and the absolute lane curvature at that sample.
    """
    r = boundary_r_at_s(lane, side, s)
    lane_position = LanePosition(s, r, 0.0)
    pos = lane.ToInertialPosition(lane_position)
    v = pos.xyz()
    return (v[0], v[1], v[2]), abs(lane.GetCurvature(lane_position))


def estimate_arc_deviation(chord_length, curvature):
    """Estimate arc-to-chord deviation for a circular arc segment.

    Args:
        chord_length: Chord length in meters between segment endpoints.
        curvature: Curvature magnitude in 1/meters.

    Returns:
        float: Estimated sagitta (arc-to-chord deviation) in meters. Returns
            0.0 for degenerate flat/zero-length input, and inf when the chord
            is not representable by the curvature radius.
    """
    if curvature <= 0.0 or chord_length <= 0.0:
        return 0.0

    radius = 1.0 / curvature
    half_chord = chord_length / 2.0
    if half_chord >= radius:
        return float("inf")

    return radius - math.sqrt(radius * radius - half_chord * half_chord)


def midpoint_deviation(start_point, midpoint, end_point):
    """Return the deviation of the sampled midpoint from the linear midpoint.

    Args:
        start_point: Segment start point as (x, y, z).
        midpoint: Sampled midpoint as (x, y, z).
        end_point: Segment end point as (x, y, z).

    Returns:
        float: Euclidean distance between sampled and linear midpoints.
    """
    linear_midpoint = tuple((start + end) / 2.0 for start, end in zip(start_point, end_point))
    return euclidean_dist(midpoint, linear_midpoint)


def sample_boundary(lane, side, max_num_samples=NUM_SAMPLES, max_chord_error=MAX_CHORD_ERROR):
    """Sample a lane boundary as a list of (x, y, z) tuples.

    Args:
        lane: A maliput Lane object.
        side: 'left' for the left (max_r) edge, 'right' for the right (min_r) edge.
        max_num_samples: Maximum number of sample points along the boundary.
        max_chord_error: Maximum allowed arc-to-chord deviation in meters.

    Returns:
        List of (x, y, z) tuples in the lane's s-increasing direction.
    """
    if max_num_samples < 2:
        raise ValueError("max_num_samples must be at least 2.")

    length = lane.length()
    cache = {}

    def get_sample(s):
        sample = cache.get(s)
        if sample is None:
            sample = sample_boundary_point(lane, side, s)
            cache[s] = sample
        return sample

    if math.isclose(length, 0.0):
        point, _ = get_sample(0.0)
        return [point, point]

    accepted_s = {0.0, length}
    segments = []
    sequence = 0

    def push_segment(s0, s1):
        nonlocal sequence

        if s1 <= s0:
            return

        mid_s = 0.5 * (s0 + s1)
        if math.isclose(mid_s, s0) or math.isclose(mid_s, s1):
            return

        start_point, start_curvature = get_sample(s0)
        midpoint, mid_curvature = get_sample(mid_s)
        end_point, end_curvature = get_sample(s1)

        curvature_bound = estimate_arc_deviation(
            euclidean_dist(start_point, end_point),
            max(start_curvature, mid_curvature, end_curvature),
        )
        deviation = max(curvature_bound, midpoint_deviation(start_point, midpoint, end_point))
        heapq.heappush(segments, (-deviation, sequence, s0, mid_s, s1))
        sequence += 1

    push_segment(0.0, length)

    while segments and len(accepted_s) < max_num_samples:
        negative_deviation, _, s0, mid_s, s1 = heapq.heappop(segments)
        deviation = -negative_deviation
        if deviation <= max_chord_error:
            break

        if mid_s in accepted_s:
            continue

        accepted_s.add(mid_s)
        push_segment(s0, mid_s)
        push_segment(mid_s, s1)

    return [get_sample(s)[0] for s in sorted(accepted_s)]


def euclidean_dist(p1, p2):
    """Compute the 3D Euclidean distance between two points.

    Args:
        p1: First point as (x, y, z).
        p2: Second point as (x, y, z).

    Returns:
        float: Euclidean distance between p1 and p2.
    """
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(p1, p2)))


def boundaries_match(pts_a, pts_b, tolerance=0.5):
    """Check if two boundary polylines represent the same physical boundary.

    Args:
        pts_a: First boundary polyline as a list of (x, y, z) points.
        pts_b: Second boundary polyline as a list of (x, y, z) points.
        tolerance: Endpoint distance tolerance in meters for matching.

    Returns:
        (True, False) if same direction,
        (True, True)  if reversed direction,
        (False, False) if no match.
    """
    if not pts_a or not pts_b:
        return False, False

    d_same_start = euclidean_dist(pts_a[0], pts_b[0])
    d_same_end = euclidean_dist(pts_a[-1], pts_b[-1])
    d_rev_start = euclidean_dist(pts_a[0], pts_b[-1])
    d_rev_end = euclidean_dist(pts_a[-1], pts_b[0])

    if d_same_start < tolerance and d_same_end < tolerance:
        return True, False  # match, not inverted
    if d_rev_start < tolerance and d_rev_end < tolerance:
        return True, True  # match, inverted
    return False, False


def lane_type_to_gpkg(lane):
    """Map maliput lane type enum to a stable GeoPackage lane_type string.

    Args:
        lane: A maliput Lane object.

    Returns:
        str: Normalized lane type string for storage in GeoPackage.
    """
    lane_type_name = str(lane.type())

    if lane_type_name.endswith("kDriving"):
        return "driving"
    if lane_type_name.endswith("kShoulder"):
        return "shoulder"
    if lane_type_name.endswith("kWalking"):
        return "walking"
    if lane_type_name.endswith("kBiking"):
        return "biking"
    if lane_type_name.endswith("kParking"):
        return "parking"

    return "other"


# -----------------------------------------------------------------------------
# Segment boundary extraction
# -----------------------------------------------------------------------------

def order_lanes_left_to_right(segment):
    """Return segment lanes ordered from leftmost to rightmost.

    Args:
        segment: A maliput Segment object.

    Returns:
        List: Segment lanes ordered from left to right.
    """
    # Start from any lane and find the leftmost
    lane = segment.lane(0)
    while lane.to_left() is not None:
        lane = lane.to_left()

    # Traverse left-to-right
    ordered = []
    while lane is not None:
        ordered.append(lane)
        lane = lane.to_right()
    return ordered


def extract_segment_boundaries(segment, max_num_samples=NUM_SAMPLES, max_chord_error=MAX_CHORD_ERROR):
    """Extract unique boundaries and lane→boundary mappings for one segment.

    For N laterally-adjacent lanes, there are N+1 unique boundaries.
    Adjacent lanes share the boundary between them.

    Args:
        segment: A maliput Segment object.
        max_num_samples: Maximum number of sample points per boundary.
        max_chord_error: Maximum allowed arc-to-chord deviation in meters.

    Returns:
        boundaries: dict mapping boundary_id → list of (x,y,z) points
        lane_info: dict mapping lane_id → {
            'left_boundary_id': str,
            'left_inverted': bool,
            'right_boundary_id': str,
            'right_inverted': bool,
        }
    """
    lanes = order_lanes_left_to_right(segment)
    seg_id = segment.id().string()

    boundaries = {}
    lane_info = {}
    boundary_idx = 0

    for lane_pos, lane in enumerate(lanes):
        lane_id = lane.id().string()
        info = {}

        # ---- Left boundary ----
        if lane_pos == 0:
            # Leftmost lane: its left boundary is new
            bid = f"{seg_id}_b{boundary_idx}"
            pts = sample_boundary(lane, "left", max_num_samples, max_chord_error)
            boundaries[bid] = pts
            info["left_boundary_id"] = bid
            info["left_inverted"] = False
            boundary_idx += 1
        else:
            # Reuse the previous lane's right boundary
            prev_lane_id = lanes[lane_pos - 1].id().string()
            shared_bid = lane_info[prev_lane_id]["right_boundary_id"]
            shared_pts = boundaries[shared_bid]

            # Check if this lane sees the shared boundary in the same direction
            my_left_pts = sample_boundary(lane, "left", max_num_samples, max_chord_error)
            match, inverted = boundaries_match(shared_pts, my_left_pts)
            if match:
                info["left_boundary_id"] = shared_bid
                info["left_inverted"] = inverted
            else:
                # Fallback: store as a new boundary (shouldn't happen normally)
                bid = f"{seg_id}_b{boundary_idx}"
                boundaries[bid] = my_left_pts
                info["left_boundary_id"] = bid
                info["left_inverted"] = False
                boundary_idx += 1

        # ---- Right boundary ----
        bid = f"{seg_id}_b{boundary_idx}"
        pts = sample_boundary(lane, "right", max_num_samples, max_chord_error)
        boundaries[bid] = pts
        info["right_boundary_id"] = bid
        info["right_inverted"] = False
        boundary_idx += 1

        lane_info[lane_id] = info

    return boundaries, lane_info


# -----------------------------------------------------------------------------
# Branch point extraction
# -----------------------------------------------------------------------------

def extract_branch_points(road_geometry):
    """Extract branch point data for the branch_point_lanes table.

    Args:
        road_geometry: A maliput RoadGeometry object.

    Returns:
        list of (branch_point_id, lane_id, side, lane_end) tuples.
    """
    rows = []
    for bp_idx in range(road_geometry.num_branch_points()):
        bp = road_geometry.branch_point(bp_idx)
        bp_id = bp.id().string()

        a_side = bp.GetASide()
        for i in range(a_side.size()):
            le = a_side.get(i)
            lane_id = le.lane.id().string()
            lane_end = "start" if le.end == Which.kStart else "finish"
            rows.append((bp_id, lane_id, "a", lane_end))

        b_side = bp.GetBSide()
        for i in range(b_side.size()):
            le = b_side.get(i)
            lane_id = le.lane.id().string()
            lane_end = "start" if le.end == Which.kStart else "finish"
            rows.append((bp_id, lane_id, "b", lane_end))

    return rows


# -----------------------------------------------------------------------------
# Speed limit extraction
# -----------------------------------------------------------------------------

SPEED_LIMIT_RULE_TYPE = "Speed-Limit Rule Type"


def extract_speed_limits(road_network):
    """Extract speed limit rules from the road network's rulebook.

    Queries all RangeValueRules with type "Speed-Limit Rule Type" and returns
    one row per (lane, range) combination.

    Args:
        road_network: A maliput RoadNetwork object.

    Returns:
        list of (speed_limit_id, lane_id, s_start, s_end, max_speed, min_speed,
                 description, severity) tuples.
    """
    rulebook = road_network.rulebook()
    all_rules = rulebook.Rules()
    rows = []
    sl_counter = 0

    for rule_id_str, rule in all_rules.range_value_rules.items():
        if rule.type_id().string() != SPEED_LIMIT_RULE_TYPE:
            continue

        zone = rule.zone()
        ranges = rule.states()

        for lane_s_range in zone.ranges():
            lane_id = lane_s_range.lane_id().string()
            s_range = lane_s_range.s_range()
            s_start = s_range.s0()
            s_end = s_range.s1()

            for r in ranges:
                sl_counter += 1
                speed_limit_id = f"sl_{sl_counter}"
                rows.append((
                    speed_limit_id,
                    lane_id,
                    s_start,
                    s_end,
                    r.max,
                    r.min,
                    r.description,
                    r.severity,
                ))

    return rows


# -----------------------------------------------------------------------------
# Main migration logic
# -----------------------------------------------------------------------------

def main():
    """Run the OpenDRIVE-to-GeoPackage migration workflow.

    This function parses command-line options, loads the OpenDRIVE map through
    the maliput_malidrive backend, extracts junction/segment/lane/boundary/
    branch-point data, and writes the result to a GeoPackage file using the
    maliput_geopackage schema.

    Raises:
        FileNotFoundError: If the input .xodr file or template .gpkg file does
            not exist.
    """
    args = parse_args()

    xodr_path = Path(args.xodr_file).resolve()
    if not xodr_path.exists():
        raise FileNotFoundError(f"XODR file not found: {xodr_path}")

    # Derive output path from input stem if not specified
    if args.output:
        output_gpkg = Path(args.output)
    else:
        output_gpkg = Path.cwd() / f"{xodr_path.stem}.gpkg"

    max_num_samples = args.num_samples
    max_chord_error = args.max_chord_error
    linear_tolerance = args.linear_tolerance
    angular_tolerance = args.angular_tolerance
    omit_nondrivable = "true" if args.omit_nondrivable_lanes else "false"

    print(f"Loading XODR: {xodr_path}")

    # ---- 1. Load the road network via maliput_malidrive ----
    road_network = maliput.plugin.create_road_network(
        "maliput_malidrive",
        {
            "opendrive_file": str(xodr_path),
            "linear_tolerance": linear_tolerance,
            "angular_tolerance": angular_tolerance,
            "standard_strictness_policy": "permissive",
            "omit_nondrivable_lanes": omit_nondrivable,
        },
    )
    rg = road_network.road_geometry()

    print(f"Road geometry: {rg.id().string()}")
    print(f"  Junctions:     {rg.num_junctions()}")
    print(f"  Branch points: {rg.num_branch_points()}")

    # ---- 2. Collect data from the road geometry ----
    junctions = []  # (junction_id, name)
    segments = []   # (segment_id, junction_id, name)
    all_boundaries = {}  # boundary_id → [(x,y,z), ...]
    all_lanes = []  # (lane_id, segment_id, type, direction, l_bid, l_inv, r_bid, r_inv)
    total_lane_count = 0

    for j_idx in range(rg.num_junctions()):
        junction = rg.junction(j_idx)
        j_id = junction.id().string()
        junctions.append((j_id, j_id))

        for s_idx in range(junction.num_segments()):
            segment = junction.segment(s_idx)
            s_id = segment.id().string()
            segments.append((s_id, j_id, s_id))

            # Extract boundaries and per-lane mapping
            seg_boundaries, seg_lane_info = extract_segment_boundaries(
                segment,
                max_num_samples,
                max_chord_error,
            )
            all_boundaries.update(seg_boundaries)

            for l_idx in range(segment.num_lanes()):
                lane = segment.lane(l_idx)
                l_id = lane.id().string()
                info = seg_lane_info[l_id]
                all_lanes.append((
                    l_id,
                    s_id,
                    lane_type_to_gpkg(lane),
                    "forward",    # default direction
                    info["left_boundary_id"],
                    info["left_inverted"],
                    info["right_boundary_id"],
                    info["right_inverted"],
                ))
                total_lane_count += 1

    branch_point_rows = extract_branch_points(rg)

    speed_limit_rows = extract_speed_limits(road_network)

    print(f"  Lanes:         {total_lane_count}")
    print(f"  Boundaries:    {len(all_boundaries)}")
    print(f"  BP entries:    {len(branch_point_rows)}")
    print(f"  Speed limits:  {len(speed_limit_rows)}")

    # ---- 3. Write the GeoPackage ----
    template_gpkg = Path(args.template).resolve()
    if not template_gpkg.exists():
        raise FileNotFoundError(f"Template GeoPackage not found: {template_gpkg}")

    shutil.copy(template_gpkg, output_gpkg)
    db = sqlite3.connect(str(output_gpkg))

    try:
        # -- Metadata --
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("linear_tolerance", linear_tolerance),
        )
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("angular_tolerance", angular_tolerance),
        )
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("max_chord_error", str(max_chord_error)),
        )
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("source_format", "OpenDRIVE"),
        )
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("source_file", xodr_path.name),
        )

        # -- Junctions --
        db.executemany(
            "INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
            junctions,
        )

        # -- Segments --
        db.executemany(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            segments,
        )

        # -- Lane boundaries --
        for bid, pts in all_boundaries.items():
            blob = build_gpkg_linestring_z(pts, SRS_ID)
            db.execute(
                "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)",
                (bid, blob),
            )

        # -- Lanes --
        db.executemany(
            """
            INSERT INTO lanes (
                lane_id, segment_id, lane_type, direction,
                left_boundary_id, left_boundary_inverted,
                right_boundary_id, right_boundary_inverted
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            all_lanes,
        )

        # -- Branch points --
        db.executemany(
            """
            INSERT INTO branch_point_lanes
                (branch_point_id, lane_id, side, lane_end)
            VALUES (?, ?, ?, ?)
            """,
            branch_point_rows,
        )

        # -- Speed limits --
        if speed_limit_rows:
            db.executemany(
                """
                INSERT INTO speed_limits
                    (speed_limit_id, lane_id, s_start, s_end, max_speed, min_speed,
                     description, severity)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                speed_limit_rows,
            )

        db.commit()
        print(f"\nCreated GeoPackage: {output_gpkg.resolve()}")

    finally:
        db.close()


# -----------------------------------------------------------------------------

if __name__ == "__main__":
    main()
