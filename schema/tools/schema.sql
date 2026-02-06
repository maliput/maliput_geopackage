-- Maliput GeoPackage Schema Definition
-- This file defines the complete SQLite schema for maliput GeoPackage databases.
-- Language-specific tools load and execute this schema.

-- Core Tables

CREATE TABLE maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE junctions (
    junction_id TEXT PRIMARY KEY,
    name TEXT
);

CREATE TABLE segments (
    segment_id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    name TEXT,
    FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
);

CREATE TABLE boundaries (
    boundary_id TEXT PRIMARY KEY,
    geometry TEXT NOT NULL
);

CREATE TABLE lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving',
    direction TEXT DEFAULT 'forward',
    left_boundary_id TEXT NOT NULL,
    left_boundary_inverted BOOLEAN DEFAULT FALSE,
    right_boundary_id TEXT NOT NULL,
    right_boundary_inverted BOOLEAN DEFAULT FALSE,
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id),
    FOREIGN KEY (left_boundary_id) REFERENCES boundaries(boundary_id),
    FOREIGN KEY (right_boundary_id) REFERENCES boundaries(boundary_id)
);

-- Connectivity Tables

CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);

CREATE VIEW adjacent_lanes AS
SELECT
    l1.lane_id AS lane_id,
    l2.lane_id AS adjacent_lane_id,
    CASE
        WHEN l1.right_boundary_id = l2.left_boundary_id THEN 'right'
        WHEN l1.left_boundary_id  = l2.right_boundary_id THEN 'left'
        ELSE NULL
    END AS side
FROM lanes l1
JOIN lanes l2
    ON l1.right_boundary_id = l2.left_boundary_id
    OR l1.left_boundary_id  = l2.right_boundary_id
WHERE l1.lane_id <> l2.lane_id
    AND (
        l1.right_boundary_id = l2.left_boundary_id
        OR l1.left_boundary_id  = l2.right_boundary_id
    );

-- Road Marking Tables

CREATE TABLE lane_markings (
    marking_id TEXT PRIMARY KEY,
    boundary_id TEXT NOT NULL,
    s_start REAL NOT NULL,
    s_end REAL NOT NULL,
    marking_type TEXT NOT NULL,
    color TEXT DEFAULT 'white',
    weight TEXT DEFAULT 'standard',
    width REAL,
    height REAL,
    material TEXT,
    lane_change_rule TEXT DEFAULT 'none',
    FOREIGN KEY (boundary_id) REFERENCES boundaries(boundary_id),
    CHECK (s_start >= 0 AND s_end >= s_start)
);

CREATE TABLE lane_marking_lines (
    line_id INTEGER PRIMARY KEY AUTOINCREMENT,
    marking_id TEXT NOT NULL,
    line_index INTEGER NOT NULL,
    length REAL,
    space REAL,
    width REAL,
    r_offset REAL,
    color TEXT,
    FOREIGN KEY (marking_id) REFERENCES lane_markings(marking_id)
);

-- Traffic Control Tables

CREATE TABLE traffic_lights (
    traffic_light_id TEXT PRIMARY KEY,
    inertial_x REAL NOT NULL,
    inertial_y REAL NOT NULL,
    inertial_z REAL NOT NULL,
    roll REAL DEFAULT 0.0,
    pitch REAL DEFAULT 0.0,
    yaw REAL DEFAULT 0.0,
    name TEXT
);

CREATE TABLE bulb_groups (
    bulb_group_id TEXT PRIMARY KEY,
    traffic_light_id TEXT NOT NULL,
    relative_x REAL DEFAULT 0.0,
    relative_y REAL DEFAULT 0.0,
    relative_z REAL DEFAULT 0.0,
    roll REAL DEFAULT 0.0,
    pitch REAL DEFAULT 0.0,
    yaw REAL DEFAULT 0.0,
    name TEXT,
    FOREIGN KEY (traffic_light_id) REFERENCES traffic_lights(traffic_light_id)
);

CREATE TABLE bulbs (
    bulb_id TEXT PRIMARY KEY,
    bulb_group_id TEXT NOT NULL,
    relative_x REAL DEFAULT 0.0,
    relative_y REAL DEFAULT 0.0,
    relative_z REAL DEFAULT 0.0,
    color TEXT NOT NULL CHECK (color IN ('red', 'yellow', 'green')),
    bulb_type TEXT NOT NULL CHECK (bulb_type IN ('round', 'arrow')),
    FOREIGN KEY (bulb_group_id) REFERENCES bulb_groups(bulb_group_id)
);

CREATE TABLE stop_lines (
    stop_line_id TEXT PRIMARY KEY,
    lane_id TEXT NOT NULL,
    s_position REAL NOT NULL,
    geometry TEXT NOT NULL,
    traffic_light_id TEXT,
    allow_passing BOOLEAN DEFAULT FALSE,
    name TEXT,
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
    FOREIGN KEY (traffic_light_id) REFERENCES traffic_lights(traffic_light_id),
    CHECK (s_position >= 0)
);
