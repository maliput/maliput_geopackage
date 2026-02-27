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

# Sampling resolution: number of points per lane boundary
NUM_SAMPLES = 50

# GeoPackage SRS ID for the local Cartesian coordinate system
SRS_ID = 100000

# Tolerances used when loading the XODR file
LINEAR_TOLERANCE = "5e-2"
ANGULAR_TOLERANCE = "1e-3"


# -----------------------------------------------------------------------------
# CLI argument parsing
# -----------------------------------------------------------------------------

def parse_args():
    """Parse command-line arguments."""
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
        help=f"Number of sample points per lane boundary (default: {NUM_SAMPLES}).",
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
        default=False,
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

def sample_boundary(lane, side, num_samples=NUM_SAMPLES):
    """Sample a lane boundary as a list of (x, y, z) tuples.

    Args:
        lane: A maliput Lane object.
        side: 'left' for the left (max_r) edge, 'right' for the right (min_r) edge.
        num_samples: Number of evenly-spaced samples along s.

    Returns:
        List of (x, y, z) tuples in the lane's s-increasing direction.
    """
    points = []
    length = lane.length()
    for i in range(num_samples):
        s = length * i / (num_samples - 1)
        bounds = lane.lane_bounds(s)
        r = bounds.max_r() if side == "left" else bounds.min_r()
        pos = lane.ToInertialPosition(LanePosition(s, r, 0.0))
        v = pos.xyz()
        points.append((v[0], v[1], v[2]))
    return points


def euclidean_dist(p1, p2):
    """3D Euclidean distance between two (x,y,z) tuples."""
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(p1, p2)))


def boundaries_match(pts_a, pts_b, tolerance=0.5):
    """Check if two boundary polylines represent the same physical boundary.

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


# -----------------------------------------------------------------------------
# Segment boundary extraction
# -----------------------------------------------------------------------------

def order_lanes_left_to_right(segment):
    """Return a list of lanes in a segment ordered from leftmost to rightmost."""
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


def extract_segment_boundaries(segment, num_samples=NUM_SAMPLES):
    """Extract unique boundaries and lane→boundary mappings for one segment.

    For N laterally-adjacent lanes, there are N+1 unique boundaries.
    Adjacent lanes share the boundary between them.

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
            pts = sample_boundary(lane, "left", num_samples)
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
            my_left_pts = sample_boundary(lane, "left", num_samples)
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
        pts = sample_boundary(lane, "right", num_samples)
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
# Main migration logic
# -----------------------------------------------------------------------------

def main():
    args = parse_args()

    xodr_path = Path(args.xodr_file).resolve()
    if not xodr_path.exists():
        raise FileNotFoundError(f"XODR file not found: {xodr_path}")

    # Derive output path from input stem if not specified
    if args.output:
        output_gpkg = Path(args.output)
    else:
        output_gpkg = Path.cwd() / f"{xodr_path.stem}.gpkg"

    num_samples = args.num_samples
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
            seg_boundaries, seg_lane_info = extract_segment_boundaries(segment, num_samples)
            all_boundaries.update(seg_boundaries)

            for l_idx in range(segment.num_lanes()):
                lane = segment.lane(l_idx)
                l_id = lane.id().string()
                info = seg_lane_info[l_id]
                all_lanes.append((
                    l_id,
                    s_id,
                    # all lanes are set to driving. We should use lane.type() to determine the actual type,
                    # but malidrive's lane types are not fully supported in the current maliput python API version.
                    "driving",
                    "forward",    # default direction
                    info["left_boundary_id"],
                    info["left_inverted"],
                    info["right_boundary_id"],
                    info["right_inverted"],
                ))
                total_lane_count += 1

    branch_point_rows = extract_branch_points(rg)

    print(f"  Lanes:         {total_lane_count}")
    print(f"  Boundaries:    {len(all_boundaries)}")
    print(f"  BP entries:    {len(branch_point_rows)}")

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

        db.commit()
        print(f"\nCreated GeoPackage: {output_gpkg.resolve()}")

    finally:
        db.close()


# -----------------------------------------------------------------------------

if __name__ == "__main__":
    main()
