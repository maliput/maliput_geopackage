#!/usr/bin/env python3

import shutil
import sqlite3
import struct
from pathlib import Path
from osgeo import ogr

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

TEMPLATE_GPKG = Path("../schema/tools/template.gpkg")
OUTPUT_GPKG = Path("two_lane_road.gpkg")

ROAD_LENGTH = 100.0
LANE_HALF_WIDTH = 3.5  # meters

# -----------------------------------------------------------------------------
# Geometry helpers (Option B: WKB + GeoPackage header)
# -----------------------------------------------------------------------------

def geometry_to_gpkg_binary(geom, srs_id=0):
    """
    Convert an OGR geometry into GeoPackageBinary (2D, no envelope).
    """
    # GeoPackage header (8 bytes)
    magic = b"GP"       # 0x47 0x50
    version = 0
    flags = 0x01        # little endian, no envelope

    header = struct.pack("<2sBBi", magic, version, flags, srs_id)

    # WKB from GDAL (little endian)
    wkb = geom.ExportToWkb(ogr.wkbNDR)

    return header + wkb


def linestring_to_gpkg(points, srs_id=0):
    geom = ogr.Geometry(ogr.wkbLineString25D)
    for x, y in points:
        geom.AddPoint(x, y, 0.0)
    return geometry_to_gpkg_binary(geom, srs_id)

def polygon_to_gpkg(points, srs_id=0):
    ring = ogr.Geometry(ogr.wkbLinearRing)
    for x, y in points:
        ring.AddPoint(x, y)
    ring.CloseRings()

    poly = ogr.Geometry(ogr.wkbPolygon)
    poly.AddGeometry(ring)

    return geometry_to_gpkg_binary(poly, srs_id)

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

def main():
    # 1. Copy template
    shutil.copy(TEMPLATE_GPKG, OUTPUT_GPKG)

    # 2. Open GeoPackage (SQLite)
    db = sqlite3.connect(OUTPUT_GPKG)

    try:
        # ---------------------------------------------------------------------
        # Schema-level metadata
        # ---------------------------------------------------------------------
        db.execute(
            "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
            ("linear_tolerance", "0.01"),
        )

        # ---------------------------------------------------------------------
        # Domain data
        # ---------------------------------------------------------------------
        db.execute(
            "INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
            ("j1", "Main Junction"),
        )

        db.execute(
            "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
            ("seg1", "j1", "Straight Segment"),
        )

        # ---------------------------------------------------------------------
        # Lane boundaries (three lines)
        # ---------------------------------------------------------------------

        left_outer = linestring_to_gpkg(
            [(0.0, +LANE_HALF_WIDTH), (ROAD_LENGTH, +LANE_HALF_WIDTH)]
        )

        center = linestring_to_gpkg(
            [(0.0, 0.0), (ROAD_LENGTH, 0.0)]
        )

        right_outer = linestring_to_gpkg(
            [(0.0, -LANE_HALF_WIDTH), (ROAD_LENGTH, -LANE_HALF_WIDTH)]
        )

        db.execute(
            "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)",
            ("b_left_outer", left_outer),
        )
        db.execute(
            "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)",
            ("b_center", center),
        )
        db.execute(
            "INSERT INTO lane_boundaries (boundary_id, geometry) VALUES (?, ?)",
            ("b_right_outer", right_outer),
        )

        # ---------------------------------------------------------------------
        # Lanes
        # ---------------------------------------------------------------------

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
                "lane_1",
                "seg1",
                "driving",
                "forward",
                "b_left_outer",
                False,
                "b_center",
                False,
            ),
        )

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
                "lane_2",
                "seg1",
                "driving",
                "forward",
                "b_center",
                False,
                "b_right_outer",
                False,
            ),
        )

        # ---------------------------------------------------------------------
        # Connectivity
        # ---------------------------------------------------------------------

        db.executemany(
            """
            INSERT INTO branch_point_lanes
            (branch_point_id, lane_id, side, lane_end)
            VALUES (?, ?, ?, ?)
            """,
            [
                ("bp_start", "lane_1", "a", "start"),
                ("bp_start", "lane_2", "a", "start"),
                ("bp_end",   "lane_1", "b", "finish"),
                ("bp_end",   "lane_2", "b", "finish"),
            ],
        )

        # ---------------------------------------------------------------------
        # Commit
        # ---------------------------------------------------------------------

        db.commit()

    finally:
        db.close()

    print(f"Created GeoPackage: {OUTPUT_GPKG.resolve()}")

# -----------------------------------------------------------------------------

if __name__ == "__main__":
    main()
