#!/usr/bin/env python3

import math
import shutil
import sqlite3
import struct
from pathlib import Path
from osgeo import ogr

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

TEMPLATE_GPKG = Path("../schema/tools/template.gpkg")
OUTPUT_GPKG = Path("complex_road.gpkg")

# Road parameters
LANE_HALF_WIDTH = 3.5  # meters

# Segment 1: Straight segment (0-100m)
SEG1_LENGTH = 100.0

# Segment 2: Curved segment with transition (100-300m)
SEG2_LENGTH = 200.0
CURVE_RADIUS = 500.0  # meters

# Segment 3: Interchange area (300-400m)
SEG3_LENGTH = 100.0

# Segment 4: Curved exit (400-500m)
SEG4_LENGTH = 100.0

# Linear tolerance
LINEAR_TOLERANCE = 0.01

# -----------------------------------------------------------------------------
# Geometry helpers
# -----------------------------------------------------------------------------

def geometry_to_gpkg_binary(geom, srs_id=0):
    """
    Convert an OGR geometry into GeoPackageBinary (2D, no envelope).
    """
    magic = b"GP"       # 0x47 0x50
    version = 0
    flags = 0x01        # little endian, no envelope

    header = struct.pack("<2sBBi", magic, version, flags, srs_id)
    wkb = geom.ExportToWkb(ogr.wkbNDR)

    return header + wkb


def linestring_to_gpkg(points, srs_id=0):
    geom = ogr.Geometry(ogr.wkbLineString25D)
    for x, y in points:
        geom.AddPoint(x, y, 0.0)
    return geometry_to_gpkg_binary(geom, srs_id)


def generate_arc_points(center_x, center_y, radius, start_angle, end_angle,
                        num_points=50):
    """Generate points along a circular arc."""
    points = []
    angle_step = (end_angle - start_angle) / (num_points - 1)

    for i in range(num_points):
        angle = start_angle + i * angle_step
        x = center_x + radius * math.cos(math.radians(angle))
        y = center_y + radius * math.sin(math.radians(angle))
        points.append((x, y))

    return points


# Segment 1: Straight horizontal segment
def create_segment1_boundaries():
    """Create boundaries for the first straight segment.

    Returns a tuple of (boundaries, end_pose), where end_pose is
    (x, y, angle_degrees).
    """
    boundaries = {}

    # Left outer boundary
    left_outer = [(0.0, LANE_HALF_WIDTH), (SEG1_LENGTH, LANE_HALF_WIDTH)]
    boundaries['s1_left_outer'] = linestring_to_gpkg(left_outer)

    # Center boundary
    left_center = [(0.0, 0.0), (SEG1_LENGTH, 0.0)]
    boundaries['s1_left_center'] = linestring_to_gpkg(left_center)

    # Right outer boundary
    right_center = [(0.0, -LANE_HALF_WIDTH), (SEG1_LENGTH, -LANE_HALF_WIDTH)]
    boundaries['s1_right_center'] = linestring_to_gpkg(right_center)

    # End pose of the reference line (s1_left_center)
    end_pose = (SEG1_LENGTH, 0.0, 0.0)

    return boundaries, end_pose


# Segment 2: Curved segment with 3 lanes
def create_segment2_boundaries(start_pose):
    """Create boundaries for the curved segment starting from a given pose."""
    boundaries = {}
    start_x, start_y, start_angle_deg = start_pose
    start_angle_rad = math.radians(start_angle_deg)

    # For a curved road, we'll create a right turn (center below the road).
    # The reference curve (s2_left_center) starts where the previous segment's
    # reference curve ended, and is tangent to it.
    # For a right turn from a horizontal road, the center of curvature is below.
    center_x = start_x + CURVE_RADIUS * math.sin(start_angle_rad)
    center_y = start_y - CURVE_RADIUS

    # Arc spans from 90° to 70° (20 degree turn)
    start_arc_angle = 90
    end_arc_angle = 70

    # Left outer boundary (outer edge of curve)
    arc_points_outer = generate_arc_points(center_x, center_y,
                                          CURVE_RADIUS + LANE_HALF_WIDTH,
                                          start_arc_angle, end_arc_angle, num_points=40)
    boundaries['s2_left_outer'] = linestring_to_gpkg(arc_points_outer)

    # Left-center boundary (center line of first lane)
    arc_points_l1c = generate_arc_points(center_x, center_y,
                                        CURVE_RADIUS, start_arc_angle, end_arc_angle, num_points=40)
    boundaries['s2_left_center'] = linestring_to_gpkg(arc_points_l1c)

    # Center-left boundary
    arc_points_c1c = generate_arc_points(center_x, center_y,
                                        CURVE_RADIUS - LANE_HALF_WIDTH,
                                        start_arc_angle, end_arc_angle, num_points=40)
    boundaries['s2_center_left'] = linestring_to_gpkg(arc_points_c1c)

    # Center-right boundary
    arc_points_c2c = generate_arc_points(center_x, center_y,
                                        CURVE_RADIUS - 2 * LANE_HALF_WIDTH,
                                        start_arc_angle, end_arc_angle, num_points=40)
    boundaries['s2_center_right'] = linestring_to_gpkg(arc_points_c2c)

    # Calculate end pose of the reference line (s2_left_center)
    end_arc_angle_rad = math.radians(end_arc_angle)
    end_ref_x = center_x + CURVE_RADIUS * math.cos(end_arc_angle_rad)
    end_ref_y = center_y + CURVE_RADIUS * math.sin(end_arc_angle_rad)
    end_angle_deg = end_arc_angle - 90  # Tangent angle

    end_pose = (end_ref_x, end_ref_y, end_angle_deg)

    return boundaries, end_pose


# Segment 3: Interchange area
def create_segment3_boundaries(start_pose):
    """Create boundaries for the interchange segment, starting from a pose."""
    boundaries = {}
    start_x, start_y, start_angle_deg = start_pose
    start_angle_rad = math.radians(start_angle_deg)

    # For 2 lanes, we need 3 boundaries. Assume they are centered on the ref line.
    offsets = [LANE_HALF_WIDTH, 0.0, -LANE_HALF_WIDTH]
    boundary_ids = ['s3_left_outer', 's3_left_center', 's3_right_center']

    for i, offset in enumerate(offsets):
        p_start_x = start_x - offset * math.sin(start_angle_rad)
        p_start_y = start_y + offset * math.cos(start_angle_rad)
        p_end_x = p_start_x + SEG3_LENGTH * math.cos(start_angle_rad)
        p_end_y = p_start_y + SEG3_LENGTH * math.sin(start_angle_rad)
        boundaries[boundary_ids[i]] = linestring_to_gpkg([(p_start_x, p_start_y), (p_end_x, p_end_y)])

    end_ref_x = start_x + SEG3_LENGTH * math.cos(start_angle_rad)
    end_ref_y = start_y + SEG3_LENGTH * math.sin(start_angle_rad)
    end_pose = (end_ref_x, end_ref_y, start_angle_deg)

    return boundaries, end_pose


# Segment 4: Exit ramp
def create_segment4_boundaries(start_pose):
    """Create boundaries for the exit ramp segment, starting from a pose."""
    boundaries = {}
    start_x, start_y, start_angle_deg = start_pose
    start_angle_rad = math.radians(start_angle_deg)

    # Single lane, centered on the reference line. Width is LANE_HALF_WIDTH.
    offsets = [LANE_HALF_WIDTH, 0.0, -LANE_HALF_WIDTH]
    boundary_ids = ['s4_left_outer', 's4_left_center', 's4_right_center']

    for i, offset in enumerate(offsets):
        p_start_x = start_x - offset * math.sin(start_angle_rad)
        p_start_y = start_y + offset * math.cos(start_angle_rad)
        p_end_x = p_start_x + SEG4_LENGTH * math.cos(start_angle_rad)
        p_end_y = p_start_y + SEG4_LENGTH * math.sin(start_angle_rad)
        boundaries[boundary_ids[i]] = linestring_to_gpkg([(p_start_x, p_start_y), (p_end_x, p_end_y)])

    return boundaries


# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

def main():
    # 1. Copy template
    shutil.copy(TEMPLATE_GPKG, OUTPUT_GPKG)

    # 2. Open GeoPackage (SQLite)
    db = sqlite3.connect(OUTPUT_GPKG)

    try:
        # Meta-data
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("linear_tolerance", str(LINEAR_TOLERANCE)),
        )

        # Junctions
        db.execute(
            "INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
            ("j1", "Main Highway"),
        )
        db.execute(
            "INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
            ("j2", "Interchange"),
        )

        # Segments
        db.execute(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            ("seg1", "j1", "Straight Entry"),
        )
        db.execute(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            ("seg2", "j1", "Left Curve"),
        )
        db.execute(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            ("seg3", "j2", "Interchange"),
        )
        db.execute(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            ("seg4", "j2", "Exit Ramp"),
        )

        # Create all boundaries
        all_boundaries = {}

        s1_boundaries, s1_end_pose = create_segment1_boundaries()
        all_boundaries.update(s1_boundaries)

        s2_boundaries, s2_end_pose = create_segment2_boundaries(s1_end_pose)
        all_boundaries.update(s2_boundaries)

        # To transition from 3 lanes to 2, we shift the reference line to
        # keep the road centered.
        # Seg2 road center is at y = -LANE_HALF_WIDTH/2 relative to its ref line.
        # Seg3 road center is at y = 0 relative to its ref line.
        # We shift the start of seg3 to align the centers.
        s2_end_x, s2_end_y, s2_end_angle_deg = s2_end_pose
        s2_end_angle_rad = math.radians(s2_end_angle_deg)
        shift = -LANE_HALF_WIDTH / 2.0
        s3_start_x = s2_end_x - shift * math.sin(s2_end_angle_rad)
        s3_start_y = s2_end_y + shift * math.cos(s2_end_angle_rad)
        s3_start_pose = (s3_start_x, s3_start_y, s2_end_angle_deg)

        s3_boundaries, s3_end_pose = create_segment3_boundaries(s3_start_pose)
        all_boundaries.update(s3_boundaries)

        # For the exit ramp, the 2-lane road becomes a 1-lane road.
        # The reference lines can connect directly if we assume the exit ramp
        # continues from the center of the previous segment.
        s4_boundaries = create_segment4_boundaries(s3_end_pose)
        all_boundaries.update(s4_boundaries)
        # Insert boundaries into database
        for boundary_id, geom_data in all_boundaries.items():
            db.execute(
                "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)",
                (boundary_id, geom_data),
            )

        # Create lanes for segment 1 (2 lanes)
        lanes_s1 = [
            {
                "lane_id": "seg1_lane1",
                "segment_id": "seg1",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s1_left_outer",
                "left_boundary_inverted": False,
                "right_boundary_id": "s1_left_center",
                "right_boundary_inverted": False,
            },
            {
                "lane_id": "seg1_lane2",
                "segment_id": "seg1",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s1_left_center",
                "left_boundary_inverted": False,
                "right_boundary_id": "s1_right_center",
                "right_boundary_inverted": False,
            },
        ]

        # Create lanes for segment 2 (3 lanes)
        lanes_s2 = [
            {
                "lane_id": "seg2_lane1",
                "segment_id": "seg2",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s2_left_outer",
                "left_boundary_inverted": False,
                "right_boundary_id": "s2_left_center",
                "right_boundary_inverted": False,
            },
            {
                "lane_id": "seg2_lane2",
                "segment_id": "seg2",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s2_left_center",
                "left_boundary_inverted": False,
                "right_boundary_id": "s2_center_left",
                "right_boundary_inverted": False,
            },
            {
                "lane_id": "seg2_lane3",
                "segment_id": "seg2",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s2_center_left",
                "left_boundary_inverted": False,
                "right_boundary_id": "s2_center_right",
                "right_boundary_inverted": False,
            },
        ]

        # Create lanes for segment 3 (2 lanes)
        lanes_s3 = [
            {
                "lane_id": "seg3_lane1",
                "segment_id": "seg3",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s3_left_outer",
                "left_boundary_inverted": False,
                "right_boundary_id": "s3_left_center",
                "right_boundary_inverted": False,
            },
            {
                "lane_id": "seg3_lane2",
                "segment_id": "seg3",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s3_left_center",
                "left_boundary_inverted": False,
                "right_boundary_id": "s3_right_center",
                "right_boundary_inverted": False,
            },
        ]

        # Create lanes for segment 4 (1 lane)
        lanes_s4 = [
            {
                "lane_id": "seg4_lane1",
                "segment_id": "seg4",
                "lane_type": "driving",
                "direction": "forward",
                "left_boundary_id": "s4_left_boundary",
                "left_boundary_inverted": False,
                "right_boundary_id": "s4_right_boundary",
                "right_boundary_inverted": False,
            },
        ]

        # Insert all lanes
        all_lanes = lanes_s1 + lanes_s2 + lanes_s3 + lanes_s4

        for lane in all_lanes:
            db.execute(
                """
                INSERT INTO lanes (
                    lane_id,
                    segment_id,
                    lane_type,
                    direction,
                    left_boundary_id,
                    left_boundary_inverted,
                    right_boundary_id,
                    right_boundary_inverted
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    lane["lane_id"],
                    lane["segment_id"],
                    lane["lane_type"],
                    lane["direction"],
                    lane["left_boundary_id"],
                    lane["left_boundary_inverted"],
                    lane["right_boundary_id"],
                    lane["right_boundary_inverted"],
                ),
            )

        # Define branch points for connectivity
        branch_points_data = [
            # Segment 1 start/end
            ("bp_seg1_start", "seg1_lane1", "a", "start"),
            ("bp_seg1_start", "seg1_lane2", "a", "start"),
            ("bp_seg1_end", "seg1_lane1", "b", "finish"),
            ("bp_seg1_end", "seg1_lane2", "b", "finish"),

            # Segment 2 start/end
            ("bp_seg2_start", "seg2_lane1", "a", "start"),
            ("bp_seg2_start", "seg2_lane2", "a", "start"),
            ("bp_seg2_start", "seg2_lane3", "a", "start"),
            ("bp_seg2_end", "seg2_lane1", "b", "finish"),
            ("bp_seg2_end", "seg2_lane2", "b", "finish"),
            ("bp_seg2_end", "seg2_lane3", "b", "finish"),

            # Segment 3 start/end
            ("bp_seg3_start", "seg3_lane1", "a", "start"),
            ("bp_seg3_start", "seg3_lane2", "a", "start"),
            ("bp_seg3_end", "seg3_lane1", "b", "finish"),
            ("bp_seg3_end", "seg3_lane2", "b", "finish"),

            # Segment 4 start/end
            ("bp_seg4_start", "seg4_lane1", "a", "start"),
            ("bp_seg4_end", "seg4_lane1", "b", "finish"),
        ]

        db.executemany(
            """
            INSERT INTO branch_point_lanes
            (branch_point_id, lane_id, side, lane_end)
            VALUES (?, ?, ?, ?)
            """,
            branch_points_data,
        )

        # Commit
        db.commit()

    finally:
        db.close()

    print(f"Created GeoPackage: {OUTPUT_GPKG.resolve()}")
    print(f"  - 4 segments: straight, curved, interchange, and exit ramp")
    print(f"  - Total road length: {SEG1_LENGTH + SEG2_LENGTH + SEG3_LENGTH + SEG4_LENGTH}m")
    print(f"  - Multiple lanes per segment (up to 3 lanes)")
    print(f"  - Curved geometry with smooth arc transitions")


# -----------------------------------------------------------------------------

if __name__ == "__main__":
    main()
