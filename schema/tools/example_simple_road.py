#!/usr/bin/env python3
# BSD 3-Clause License
#
# Copyright (c) 2024, Woven by Toyota

"""
Example: Create a simple 2-lane road using the shared schema.

This example shows how to create a maliput GeoPackage database using
standard Python sqlite3 module and the schema.sql file.
"""

import sqlite3
from pathlib import Path


def load_schema(db: sqlite3.Connection, schema_path: str) -> None:
    """Load the maliput schema from schema.sql."""
    with open(schema_path, 'r') as f:
        schema_sql = f.read()
    db.executescript(schema_sql)


def create_simple_road():
    """Create a basic 2-lane straight road."""

    # Find schema.sql in the same directory as this script
    script_dir = Path(__file__).parent
    schema_path = script_dir / 'schema.sql'

    # Create database and load schema
    db = sqlite3.connect('simple_road.gpkg')
    load_schema(db, str(schema_path))

    # Add metadata
    db.execute("INSERT INTO maliput_metadata (key, value) VALUES (?, ?)",
               ('linear_tolerance', '0.01'))
    db.execute("INSERT INTO maliput_metadata (key, value) VALUES (?, ?)",
               ('angular_tolerance', '0.01'))

    # Add junction and segment
    db.execute("INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
               ('j1', 'Main Junction'))
    db.execute("INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
               ('j1_s1', 'j1', 'Main Segment'))

    # Add boundaries
    db.execute("INSERT INTO boundaries (boundary_id, geometry) VALUES (?, ?)",
               ('b_right', 'LINESTRINGZ(0 0 0, 100 0 0)'))
    db.execute("INSERT INTO boundaries (boundary_id, geometry) VALUES (?, ?)",
               ('b_center', 'LINESTRINGZ(0 3.5 0, 100 3.5 0)'))
    db.execute("INSERT INTO boundaries (boundary_id, geometry) VALUES (?, ?)",
               ('b_left', 'LINESTRINGZ(0 7.0 0, 100 7.0 0)'))

    # Add lanes
    db.execute("""INSERT INTO lanes
                  (lane_id, segment_id, left_boundary_id, right_boundary_id)
                  VALUES (?, ?, ?, ?)""",
               ('lane1', 'j1_s1', 'b_center', 'b_right'))
    db.execute("""INSERT INTO lanes
                  (lane_id, segment_id, left_boundary_id, right_boundary_id)
                  VALUES (?, ?, ?, ?)""",
               ('lane2', 'j1_s1', 'b_left', 'b_center'))

    # Add lane markings
    db.execute("""INSERT INTO lane_markings
                  (marking_id, boundary_id, s_start, s_end, marking_type, color, width, lane_change_rule)
                  VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
               ('mark_center', 'b_center', 0.0, 100.0, 'dashed', 'white', 0.15, 'allowed'))

    # Add traffic light
    db.execute("""INSERT INTO traffic_lights
                  (traffic_light_id, inertial_x, inertial_y, inertial_z, name)
                  VALUES (?, ?, ?, ?, ?)""",
               ('tl_1', 50.0, 10.0, 4.5, 'Traffic Light 1'))

    # Add bulb group and bulbs
    db.execute("""INSERT INTO bulb_groups
                  (bulb_group_id, traffic_light_id, name)
                  VALUES (?, ?, ?)""",
               ('bg_1', 'tl_1', 'Vehicle Signal'))

    db.execute("""INSERT INTO bulbs
                  (bulb_id, bulb_group_id, color, bulb_type, relative_z)
                  VALUES (?, ?, ?, ?, ?)""",
               ('bulb_red', 'bg_1', 'red', 'round', 0.4))
    db.execute("""INSERT INTO bulbs
                  (bulb_id, bulb_group_id, color, bulb_type, relative_z)
                  VALUES (?, ?, ?, ?, ?)""",
               ('bulb_green', 'bg_1', 'green', 'round', -0.4))

    # Add stop line
    db.execute("""INSERT INTO stop_lines
                  (stop_line_id, lane_id, s_position, geometry, traffic_light_id, name)
                  VALUES (?, ?, ?, ?, ?, ?)""",
               ('stop_1', 'lane1', 90.0, 'LINESTRINGZ(90 0 0, 90 3.5 0)', 'tl_1', 'Stop Line'))

    db.commit()
    db.close()
