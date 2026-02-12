-- ============================================================================
-- Maliput GeoPackage Schema (OGC GeoPackage 1.3 compliant)
-- ============================================================================

-- ---------------------------------------------------------------------------
-- GeoPackage identification
-- ---------------------------------------------------------------------------

PRAGMA application_id = 0x47504B47;  -- 'GPKG'
PRAGMA user_version  = 10300;       -- GeoPackage version 1.3.0

-- ---------------------------------------------------------------------------
-- REQUIRED CORE GEOPACKAGE TABLES
-- ---------------------------------------------------------------------------

CREATE TABLE gpkg_spatial_ref_sys (
    srs_name TEXT NOT NULL,
    srs_id INTEGER PRIMARY KEY,
    organization TEXT NOT NULL,
    organization_coordsys_id INTEGER NOT NULL,
    definition TEXT NOT NULL,
    description TEXT
);

CREATE TABLE gpkg_contents (
    table_name TEXT NOT NULL PRIMARY KEY,
    data_type TEXT NOT NULL,
    identifier TEXT UNIQUE,
    description TEXT DEFAULT '',
    last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    min_x DOUBLE,
    min_y DOUBLE,
    max_x DOUBLE,
    max_y DOUBLE,
    srs_id INTEGER,
    CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id)
        REFERENCES gpkg_spatial_ref_sys(srs_id)
);

CREATE TABLE gpkg_geometry_columns (
    table_name TEXT NOT NULL,
    column_name TEXT NOT NULL,
    geometry_type_name TEXT NOT NULL,
    srs_id INTEGER NOT NULL,
    z TINYINT NOT NULL,
    m TINYINT NOT NULL,
    CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name),
    CONSTRAINT fk_geom_table FOREIGN KEY (table_name)
        REFERENCES gpkg_contents(table_name),
    CONSTRAINT fk_geom_srs FOREIGN KEY (srs_id)
        REFERENCES gpkg_spatial_ref_sys(srs_id)
);

CREATE TABLE gpkg_extensions (
    table_name TEXT,
    column_name TEXT,
    extension_name TEXT NOT NULL,
    definition TEXT NOT NULL,
    scope TEXT NOT NULL,
    CONSTRAINT ge_tce UNIQUE (table_name, column_name, extension_name)
);

-- ---------------------------------------------------------------------------
-- REQUIRED SRS ENTRIES
-- ---------------------------------------------------------------------------

INSERT INTO gpkg_spatial_ref_sys (
    srs_name,
    srs_id,
    organization,
    organization_coordsys_id,
    definition,
    description
) VALUES (
    'maliput_local_cartesian',
    100000,
    'MALIPUT',
    1,
    'LOCAL_CS["maliput",
        LOCAL_DATUM["map_origin", 0],
        UNIT["metre", 1],
        AXIS["x", EAST],
        AXIS["y", NORTH],
        AXIS["z", UP]
    ]',
    'Local Cartesian coordinate system aligned with maliput inertial frame'
);

-- ============================================================================
-- MALIPUT APPLICATION METADATA
-- ============================================================================

CREATE TABLE maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT INTO maliput_metadata VALUES
('schema_name', 'maliput_geopackage'),
('schema_version', '1.0.0'),
('geometry_encoding', 'GeoPackageBinary'),
('boundary_orientation', 'true means geometry direction is inverted');

-- ============================================================================
-- NON-SPATIAL DOMAIN TABLES
-- ============================================================================

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

-- ============================================================================
-- FEATURE TABLES (SPATIAL)
-- ============================================================================

-- ---------------------------------------------------------------------------
-- Lane boundaries (LINESTRING)
-- ---------------------------------------------------------------------------

CREATE TABLE boundaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boundary_id TEXT UNIQUE NOT NULL,
    geometry BLOB NOT NULL
);

INSERT INTO gpkg_contents
(table_name, data_type, identifier, description, srs_id)
VALUES
('boundaries', 'features', 'Lane Boundaries',
 'Shared lane boundary geometries', 100000);

INSERT INTO gpkg_geometry_columns
(table_name, column_name, geometry_type_name, srs_id, z, m)
VALUES
('boundaries', 'geometry', 'LINESTRING', 100000, 1, 0);

-- ---------------------------------------------------------------------------
-- Stop lines (LINESTRING)
-- ---------------------------------------------------------------------------

CREATE TABLE stop_lines (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stop_line_id TEXT UNIQUE NOT NULL,
    lane_id TEXT NOT NULL,
    s_position REAL NOT NULL,
    geometry BLOB NOT NULL,
    traffic_light_id TEXT,
    allow_passing BOOLEAN DEFAULT FALSE,
    name TEXT,
    CHECK (s_position >= 0)
);

INSERT INTO gpkg_contents
(table_name, data_type, identifier, description, srs_id)
VALUES
('stop_lines', 'features', 'Stop Lines',
 'Lane stop line geometries', 100000);

INSERT INTO gpkg_geometry_columns
(table_name, column_name, geometry_type_name, srs_id, z, m)
VALUES
('stop_lines', 'geometry', 'LINESTRING', 100000, 1, 0);

-- ============================================================================
-- ATTRIBUTE TABLES (NON-SPATIAL)
-- ============================================================================

CREATE TABLE lanes (
    lane_id TEXT UNIQUE NOT NULL,
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

-- ---------------------------------------------------------------------------
-- Connectivity
-- ---------------------------------------------------------------------------

CREATE TABLE branch_point_lanes (
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);

CREATE VIEW view_adjacent_lanes AS
SELECT
    l1.lane_id AS lane_id,
    l2.lane_id AS adjacent_lane_id,
    CASE
        WHEN l1.right_boundary_id = l2.left_boundary_id THEN 'right'
        WHEN l1.left_boundary_id  = l2.right_boundary_id THEN 'left'
    END AS side
FROM lanes l1
JOIN lanes l2
ON (
    l1.right_boundary_id = l2.left_boundary_id
 OR l1.left_boundary_id  = l2.right_boundary_id
)
WHERE l1.lane_id <> l2.lane_id;

-- ---------------------------------------------------------------------------
-- Lane markings
-- ---------------------------------------------------------------------------

CREATE TABLE lane_markings (
    marking_id TEXT UNIQUE NOT NULL,
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
    CHECK (s_start >= 0 AND s_end >= s_start),
    FOREIGN KEY (boundary_id) REFERENCES boundaries(boundary_id)
);

CREATE TABLE lane_marking_lines (
    line_id TEXT UNIQUE NOT NULL,
    marking_id TEXT NOT NULL,
    line_index INTEGER NOT NULL,
    length REAL,
    space REAL,
    width REAL,
    r_offset REAL,
    color TEXT,
    FOREIGN KEY (marking_id) REFERENCES lane_markings(marking_id)
);

-- ============================================================================
-- TRAFFIC CONTROL (NON-SPATIAL)
-- ============================================================================

CREATE TABLE traffic_lights (
    traffic_light_id TEXT UNIQUE NOT NULL,
    inertial_x REAL NOT NULL,
    inertial_y REAL NOT NULL,
    inertial_z REAL NOT NULL,
    roll REAL DEFAULT 0.0,
    pitch REAL DEFAULT 0.0,
    yaw REAL DEFAULT 0.0,
    name TEXT
);

CREATE TABLE bulb_groups (
    bulb_group_id TEXT UNIQUE NOT NULL,
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
    bulb_id TEXT UNIQUE NOT NULL,
    bulb_group_id TEXT NOT NULL,
    relative_x REAL DEFAULT 0.0,
    relative_y REAL DEFAULT 0.0,
    relative_z REAL DEFAULT 0.0,
    color TEXT NOT NULL CHECK (color IN ('red', 'yellow', 'green')),
    bulb_type TEXT NOT NULL CHECK (bulb_type IN ('round', 'arrow')),
    FOREIGN KEY (bulb_group_id) REFERENCES bulb_groups(bulb_group_id)
);
