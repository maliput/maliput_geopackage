# Maliput GeoPackage Schema

This document describes the GeoPackage schema used by `maliput_geopackage` to store HD-map data compatible with the maliput road network abstraction.

## Overview

The maliput GeoPackage format stores road network data in a SQLite database following the [OGC GeoPackage](https://www.geopackage.org/) standard. This format provides:

- **Portability**: Single-file database that works across platforms
- **Spatial indexing**: Efficient geometric queries via SpatiaLite extensions
- **Self-documenting**: Schema is queryable and human-readable
- **Tooling support**: Compatible with QGIS, GDAL, and other GIS tools

## ToC

- [Maliput GeoPackage Schema](#maliput-geopackage-schema)
  - [Overview](#overview)
  - [ToC](#toc)
  - [Schema Definition](#schema-definition)
  - [GeoPackage compliance notes](#geopackage-compliance-notes)
    - [Core Tables](#core-tables)
      - [`maliput_metadata`](#maliput_metadata)
      - [`junctions`](#junctions)
      - [`segments`](#segments)
      - [`boundaries`](#boundaries)
      - [`lanes`](#lanes)
    - [Connectivity Tables](#connectivity-tables)
      - [`branch_point_lanes`](#branch_point_lanes)
      - [`view_adjacent_lanes`](#view_adjacent_lanes)
    - [Road Markings Tables](#road-markings-tables)
      - [`lane_markings`](#lane_markings)
      - [`lane_marking_lines`](#lane_marking_lines)
    - [Traffic Control Tables](#traffic-control-tables)
      - [`traffic_lights`](#traffic_lights)
      - [`bulb_groups`](#bulb_groups)
      - [`bulbs`](#bulbs)
      - [`stop_lines`](#stop_lines)
  - [Complete Example](#complete-example)
  - [Visual Representation](#visual-representation)

## Schema Definition

## GeoPackage compliance notes

The SQL schema in this document is designed for use with GeoPackage files:

- Mandatory GeoPackage tables: a GeoPackage used for vector features MUST include the `gpkg_spatial_ref_sys`, `gpkg_contents`, and `gpkg_geometry_columns` tables per the GeoPackage specification. These tables provide SRS definitions, a registry of content layers, and geometry metadata.
- Geometry storage: GeoPackage feature geometries are stored as GeoPackageBinary BLOBs (not as WKT text).
- Feature table primary keys: GeoPackage feature tables require an `INTEGER PRIMARY KEY` column (rowid alias) for each user feature table. Text-based UUID primary keys prevent correct linking with `gpkg_metadata_reference` and some GeoPackage tools.

Minimal example SQL:

-- create required GeoPackage core tables (from the spec)
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
    CONSTRAINT fk_gc_r_srs_id FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id)
);

CREATE TABLE gpkg_geometry_columns (
    table_name TEXT NOT NULL,
    column_name TEXT NOT NULL,
    geometry_type_name TEXT NOT NULL,
    srs_id INTEGER NOT NULL,
    z TINYINT NOT NULL,
    m TINYINT NOT NULL,
    CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name),
    CONSTRAINT uk_gc_table_name UNIQUE (table_name),
    CONSTRAINT fk_gc_tn FOREIGN KEY (table_name) REFERENCES gpkg_contents(table_name),
    CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys (srs_id)
);

-- example: `boundaries` as a proper GeoPackage feature table
CREATE TABLE boundaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boundary_key TEXT UNIQUE,
    geometry BLOB NOT NULL
);

INSERT INTO gpkg_contents (table_name, data_type, identifier, description, srs_id)
VALUES ('boundaries', 'features', 'boundaries', 'Shared lane boundaries', 4326);

INSERT INTO gpkg_geometry_columns (table_name, column_name, geometry_type_name, srs_id, z, m)
VALUES ('boundaries', 'geometry', 'LINESTRING', 4326, 2, 0);

-- set GeoPackage header values when initializing a new .gpkg file
PRAGMA application_id = 0x47504B47; -- 'GPKG' in ASCII
PRAGMA user_version = 10300; -- example GeoPackage version code (1.3.0)

### Core Tables

#### `maliput_metadata`

Stores key-value pairs for road network configuration.

```sql
CREATE TABLE maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

**Common metadata keys:**

| Key | Description | Example |
| ----- | ------------- | --------- |
| `linear_tolerance` | Tolerance for linear operations (meters) | `0.01` |
| `angular_tolerance` | Tolerance for angular operations (radians) | `0.01` |
| `scale_length` | Scale length for road geometry | `1.0` |
| `inertial_to_backend_frame_translation` | Translation vector `{x, y, z}` | `{0.0, 0.0, 0.0}` |

---

#### `junctions`

Defines junctions in the road network. A junction is a collection of segments that share common branch points.

```sql
CREATE TABLE junctions (
    junction_id TEXT PRIMARY KEY,
    name TEXT
);
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `junction_id` | TEXT | Unique identifier for the junction |
| `name` | TEXT | Human-readable name (optional) |

---

#### `segments`

Defines segments within junctions. A segment is a collection of parallel lanes.

```sql
CREATE TABLE segments (
    segment_id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    name TEXT,
    FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
);
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `segment_id` | TEXT | Unique identifier for the segment |
| `junction_id` | TEXT | Parent junction ID |
| `name` | TEXT | Human-readable name (optional) |

---

#### `boundaries`

Stores shared boundary geometries. For GeoPackage compliance, geometries should be stored as BLOB (GeoPackageBinary format). These are referenced from `lanes` by ID to avoid duplicating identical boundary geometry when adjacent lanes share a common edge.

```sql
CREATE TABLE IF NOT EXISTS boundaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boundary_id TEXT UNIQUE NOT NULL,
    geometry BLOB NOT NULL  -- GeoPackageBinary BLOB for spatial compliance
);
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `id` | INTEGER | Auto-incremented primary key (required for GeoPackage compliance) |
| `boundary_id` | TEXT | Unique identifier for the boundary (human-friendly key) |
| `geometry` | BLOB | GeoPackageBinary BLOB encoding of LINESTRINGZ geometry |

**Geometry Format:**

Boundaries are stored as Well-Known Text (WKT) 3D LineStrings:

```sql
LINESTRINGZ(x1 y1 z1, x2 y2 z2, x3 y3 z3, ...)
```

The coordinates represent points in the inertial frame (typically ENU - East-North-Up).

---

#### `lanes`

Defines lanes which reference left and right boundary geometries stored in the `boundaries` table.

```sql
CREATE TABLE lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
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
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `id` | INTEGER | Auto-incremented primary key (required for GeoPackage compliance) |
| `lane_id` | TEXT | Unique identifier for the lane (human-friendly key) |
| `segment_id` | TEXT | Parent segment ID |
| `lane_type` | TEXT | Lane type: `driving`, `shoulder`, `parking`, etc. |
| `direction` | TEXT | Travel direction: `forward`, `backward`, `bidirectional` |
| `left_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` |
| `left_boundary_inverted` | BOOLEAN | If TRUE, iterate left boundary points in reverse order |
| `right_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` |
| `right_boundary_inverted` | BOOLEAN | If TRUE, iterate right boundary points in reverse order |

---

### Connectivity Tables

#### `branch_point_lanes`

Defines how lanes connect at branch points. Branch points are the start and end points of lanes where they can connect to other lanes.

```sql
CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `branch_point_id` | TEXT | Identifier for the branch point |
| `lane_id` | TEXT | Lane connected to this branch point |
| `side` | TEXT | Side of the branch point: `a` or `b` |
| `lane_end` | TEXT | Which end of the lane: `start` or `finish` |

**Branch Point Semantics:**

- A branch point connects lane ends that meet at the same physical location
- Side `a` lanes can transition to side `b` lanes (and vice versa)
- A straight road has two branch points: one at the start, one at the end
- An intersection may have multiple lanes on each side

Example for a simple 2-lane road:

```text
Lane 1: start ──────────────────> finish
Lane 2: start ──────────────────> finish

BranchPoint "bp_start":
  - a-side: lane1/start, lane2/start
  - b-side: (empty, or connected upstream lanes)

BranchPoint "bp_end":
  - a-side: lane1/finish, lane2/finish
  - b-side: (empty, or connected downstream lanes)
```

---

#### `view_adjacent_lanes`

Defines lateral adjacency between parallel lanes in the same segment.

Adjacency between lanes is derivable from shared `boundaries` references. When two lanes share a common boundary such that one lane's `right_boundary_id` equals the other's `left_boundary_id`, they are adjacent (the second lane is on the right of the first). Likewise, when one lane's `left_boundary_id` equals another's `right_boundary_id`, the second lane is on the left of the first.

Because adjacency can be computed from `lanes.left_boundary_id` / `lanes.right_boundary_id`, we avoid duplicating this data in a table. Instead provide a read-only SQL `VIEW` that derives the adjacency on demand:

```sql
CREATE VIEW view_adjacent_lanes AS
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
```

This keeps the schema normalized and guarantees adjacency consistency without requiring maintenance logic.

---

### Road Markings Tables

Road markings describe the visual appearance of lane boundaries. They provide information about the type, color, weight, and geometry of markings at lane boundaries.

#### `lane_markings`

Stores lane marking information associated with boundaries. Lane markings can vary along the length of a boundary (s-coordinate).

```sql
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
```

| Column             | Type | Description                                                                                                                  |
| ------------------ | ---- | ---------------------------------------------------------------------------------------------------------------------------- |
| `marking_id`       | TEXT | Unique identifier for the marking                                                                                            |
| `boundary_id`      | TEXT | Reference to the boundary this marking is on                                                                                 |
| `s_start`          | REAL | Start position along the boundary (s-coordinate) in meters                                                                   |
| `s_end`            | REAL | End position along the boundary in meters                                                                                    |
| `marking_type`     | TEXT | Type of marking: `solid`, `dashed`, `double_solid`, `broken`, `double_broken`, `solid_solid`, `solid_broken`, `broken_solid` |
| `color`            | TEXT | Color of the marking: `white`, `yellow`, `red`, `blue`                                                                       |
| `weight`           | TEXT | Visual weight: `standard`, `bold`                                                                                            |
| `width`            | REAL | Width of the marking in meters (optional)                                                                                    |
| `height`           | REAL | Height of the marking in meters (optional, for raised markings)                                                              |
| `material`         | TEXT | Material description (e.g., `asphalt`, `concrete`, `paint`)                                                                  |
| `lane_change_rule` | TEXT | Passing/lane change rule: `none` (no passing), `caution` (pass with caution), `allowed` (passing allowed)                    |

**Marking Types Reference:**

- `solid`: Continuous unbroken line
- `dashed`: Dashed/intermittent line
- `double_solid`: Two parallel solid lines
- `broken`: Broken line (synonymous with dashed)
- `double_broken`: Two parallel broken lines
- `solid_solid`: Dual solid lines (same direction)
- `solid_broken`: One solid, one broken (direction dependent)
- `broken_solid`: One broken, one solid (direction dependent)

---

#### `lane_marking_lines`

Stores detailed line definitions for complex markings with multiple line components (e.g., double lines with different patterns).

```sql
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
```

| Column       | Type    | Description                                                       |
| ------------ | ------- | ----------------------------------------------------------------- |
| `line_id`    | INTEGER | Auto-incremented identifier for this line component               |
| `marking_id` | TEXT    | Reference to the parent marking                                   |
| `line_index` | INTEGER | Order of this line within the marking (0-based)                   |
| `length`     | REAL    | Length of visible part of the line in meters                      |
| `space`      | REAL    | Gap/space between repeated segments in meters                     |
| `width`      | REAL    | Width of this specific line in meters                             |
| `r_offset`   | REAL    | Lateral offset from the boundary in meters (positive = rightward) |
| `color`      | TEXT    | Color of this line component (may differ from parent marking)     |

**Notes:**

- For simple markings (single line), you can omit `lane_marking_lines` entries and use only the top-level `lane_markings` fields.
- For complex markings (double lines, multi-component patterns), populate `lane_marking_lines` to describe each component.
- `length` and `space` define repeating dash patterns: a marking repeats every `length + space` meters.
- `r_offset` allows representing markings that are offset perpendicular to the boundary direction.

**Relationship to Lanes:**

Lane markings are associated with **boundaries**, not directly with lanes. Here's how they connect:

```text
Lane
  ├─ left_boundary_id ──→ Boundary
  │                          └─ Lane Markings (on this boundary)
  │                               └─ Lane Marking Lines (detailed components)
  │
  └─ right_boundary_id ──→ Boundary
                             └─ Lane Markings (on this boundary)
                                  └─ Lane Marking Lines (detailed components)
```

**Example Workflow:**

To add a white dashed center line marking to a lane:

1. Identify the lane's shared boundary (e.g., `b_between` shared between `lane1` and `lane2`)
2. Create a `lane_marking` entry referencing that boundary:

   ```sql
   INSERT INTO lane_markings (marking_id, boundary_id, s_start, s_end, marking_type, color, width, lane_change_rule)
   VALUES ('marking_lane1_lane2_center', 'b_between', 0.0, 100.0, 'dashed', 'white', 0.12, 'allowed');
   ```

3. Optionally, add detailed `lane_marking_lines` if needed:

   ```sql
   INSERT INTO lane_marking_lines (marking_id, line_index, length, space, width)
   VALUES ('marking_lane1_lane2_center', 0, 3.0, 9.0, 0.12);
   ```

The marking now applies to the shared boundary between the two lanes, and both lanes implicitly "see" this marking on their common edge.

---

### Traffic Control Tables

Traffic lights and stop lines control vehicle movement and signal right-of-way at intersections.

#### `traffic_lights`

Stores physical traffic light devices positioned in the road network. Each traffic light has an ID, position, and orientation.

```sql
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
```

| Column             | Type | Description                                                       |
| ------------------ | ---- | ----------------------------------------------------------------- |
| `traffic_light_id` | TEXT | Unique identifier for the traffic light                           |
| `inertial_x`       | REAL | X-coordinate of traffic light position in inertial frame (meters) |
| `inertial_y`       | REAL | Y-coordinate of traffic light position in inertial frame (meters) |
| `inertial_z`       | REAL | Z-coordinate of traffic light position in inertial frame (meters) |
| `roll`             | REAL | Roll angle (rotation about x-axis) in radians                     |
| `pitch`            | REAL | Pitch angle (rotation about y-axis) in radians                    |
| `yaw`              | REAL | Yaw angle (rotation about z-axis) in radians                      |
| `name`             | TEXT | Human-readable name for the traffic light (optional)              |

**Position/Orientation Notes:**

- Position is specified in the inertial frame (typically ENU - East-North-Up).
- Rotation uses roll-pitch-yaw (Euler angles) in radians.
- Typical orientation: yaw=π points the traffic light northward, yaw=0 points eastward.

---

#### `bulb_groups`

Bulb groups are collections of bulbs within a traffic light. A traffic light may have multiple groups (e.g., one for vehicles, one for pedestrians).

```sql
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
```

| Column             | Type | Description                                          |
| ------------------ | ---- | ---------------------------------------------------- |
| `bulb_group_id`    | TEXT | Unique identifier for the bulb group                 |
| `traffic_light_id` | TEXT | Parent traffic light ID                              |
| `relative_x`       | REAL | X-position relative to parent traffic light (meters) |
| `relative_y`       | REAL | Y-position relative to parent traffic light (meters) |
| `relative_z`       | REAL | Z-position relative to parent traffic light (meters) |
| `roll`             | REAL | Roll angle relative to parent (radians)              |
| `pitch`            | REAL | Pitch angle relative to parent (radians)             |
| `yaw`              | REAL | Yaw angle relative to parent (radians)               |
| `name`             | TEXT | Human-readable name (optional)                       |

**Positioning:**

- Positions and rotations are relative to the parent traffic light.
- This allows modeling traffic light assemblies with multiple signal heads positioned around a mounting structure.

---

#### `bulbs`

Bulbs are individual light elements within a bulb group. Each bulb has a color and type (round or arrow).

```sql
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
```

| Column          | Type | Description                                             |
| --------------- | ---- | ------------------------------------------------------- |
| `bulb_id`       | TEXT | Unique identifier for the bulb                          |
| `bulb_group_id` | TEXT | Parent bulb group ID                                    |
| `relative_x`    | REAL | X-position relative to parent bulb group (meters)       |
| `relative_y`    | REAL | Y-position relative to parent bulb group (meters)       |
| `relative_z`    | REAL | Z-position relative to parent bulb group (meters)       |
| `color`         | TEXT | Color of the bulb: `red`, `yellow`, `green`             |
| `bulb_type`     | TEXT | Shape/type: `round` (circular) or `arrow` (directional) |

**Bulb States:**

Bulbs have dynamic states managed by the traffic control system (phase provider):

- `On`: Bulb is illuminated
- `Off`: Bulb is not illuminated
- `Blinking`: Bulb blinks (on/off cycles)

---

#### `stop_lines`

Stop lines are regulatory markings where vehicles must stop (e.g., at stop signs or red lights). They are line segments at specific positions on lanes.

```sql
CREATE TABLE stop_lines (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stop_line_id TEXT UNIQUE NOT NULL,
    lane_id TEXT NOT NULL,
    s_position REAL NOT NULL,
    geometry BLOB NOT NULL,
    traffic_light_id TEXT,
    allow_passing BOOLEAN DEFAULT FALSE,
    name TEXT,
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
    FOREIGN KEY (traffic_light_id) REFERENCES traffic_lights(traffic_light_id),
    CHECK (s_position >= 0)
);
```

| Column             | Type    | Description                                                                                   |
| ------------------ | ------- | --------------------------------------------------------------------------------------------- |
| `id`               | INTEGER | Auto-incremented primary key (required for GeoPackage compliance)                             |
| `stop_line_id`     | TEXT    | Unique identifier for the stop line (human-friendly key)                                      |
| `lane_id`          | TEXT    | Lane where the stop line is located                                                           |
| `s_position`       | REAL    | Longitudinal position (s-coordinate) along the lane in meters                                 |
| `geometry`         | BLOB    | GeoPackageBinary BLOB encoding of LINESTRINGZ geometry of the stop line across the lane width |
| `traffic_light_id` | TEXT    | Associated traffic light ID (optional, for controlled intersections)                          |
| `allow_passing`    | BOOLEAN | Whether vehicles may pass when no traffic light is active                                     |
| `name`             | TEXT    | Human-readable name (e.g., 'Stop at Intersection A')                                          |

**Geometry Format:**

Stop lines are represented as 3D line segments perpendicular to the lane direction:

```sql
LINESTRINGZ(x1 y1 z1, x2 y2 z2)
```

The line should span the width of the lane at the given s-position.

**Stop Line Types:**

- **Controlled by traffic light**: Reference a `traffic_light_id`. Vehicles stop when the light is red.
- **Regulatory stop**: No traffic light reference. Vehicles always must stop (stop sign).
- **Yield line**: Set `allow_passing=TRUE` to indicate vehicles may proceed cautiously.

---

## Complete Example

Here's a complete SQL script to create a GeoPackage-compliant 2-lane straight road:

```sql
-- Set GeoPackage application identification and version (must be done before creating any tables)
PRAGMA application_id = 0x47504B47;  -- 'GPKG' in ASCII
PRAGMA user_version = 10300;          -- GeoPackage version 1.3.0

-- ============================================================================
-- REQUIRED GEOPACKAGE CORE TABLES (per OGC GeoPackage specification)
-- ============================================================================

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
    CONSTRAINT fk_gc_r_srs_id FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys(srs_id)
);

CREATE TABLE gpkg_geometry_columns (
    table_name TEXT NOT NULL,
    column_name TEXT NOT NULL,
    geometry_type_name TEXT NOT NULL,
    srs_id INTEGER NOT NULL,
    z TINYINT NOT NULL,
    m TINYINT NOT NULL,
    CONSTRAINT pk_geom_cols PRIMARY KEY (table_name, column_name),
    CONSTRAINT uk_gc_table_name UNIQUE (table_name),
    CONSTRAINT fk_gc_tn FOREIGN KEY (table_name) REFERENCES gpkg_contents(table_name),
    CONSTRAINT fk_gc_srs FOREIGN KEY (srs_id) REFERENCES gpkg_spatial_ref_sys (srs_id)
);

-- ============================================================================
-- MALIPUT CUSTOM TABLES
-- ============================================================================

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
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    boundary_id TEXT UNIQUE NOT NULL,
    geometry BLOB NOT NULL
);

CREATE TABLE lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
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

CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
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
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stop_line_id TEXT UNIQUE NOT NULL,
    lane_id TEXT NOT NULL,
    s_position REAL NOT NULL,
    geometry BLOB NOT NULL,
    traffic_light_id TEXT,
    allow_passing BOOLEAN DEFAULT FALSE,
    name TEXT,
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
    FOREIGN KEY (traffic_light_id) REFERENCES traffic_lights(traffic_light_id),
    CHECK (s_position >= 0)
);

-- ============================================================================
-- GEOPACKAGE CONTENTS AND GEOMETRY REGISTRATION
-- ============================================================================

-- Register WGS 84 (EPSG:4326) as the spatial reference system
INSERT INTO gpkg_spatial_ref_sys (srs_name, srs_id, organization, organization_coordsys_id, definition, description)
VALUES (
    'WGS 84',
    4326,
    'EPSG',
    4326,
    'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]]',
    'World Geodetic System 1984'
);

-- Register boundaries as a spatial feature table
INSERT INTO gpkg_contents (table_name, data_type, identifier, description, srs_id)
VALUES ('boundaries', 'features', 'boundaries', 'Shared lane boundaries (3D LineStrings)', 4326);

INSERT INTO gpkg_geometry_columns (table_name, column_name, geometry_type_name, srs_id, z, m)
VALUES ('boundaries', 'geometry', 'LINESTRINGZ', 4326, 2, 0);

-- Register stop_lines as a spatial feature table
INSERT INTO gpkg_contents (table_name, data_type, identifier, description, srs_id)
VALUES ('stop_lines', 'features', 'stop_lines', 'Stop line geometries (3D LineStrings)', 4326);

INSERT INTO gpkg_geometry_columns (table_name, column_name, geometry_type_name, srs_id, z, m)
VALUES ('stop_lines', 'geometry', 'LINESTRINGZ', 4326, 2, 0);

-- Register other tables as attributes (non-spatial) in the GeoPackage
INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('maliput_metadata', 'attributes', 'maliput_metadata', 'Maliput configuration metadata');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('junctions', 'attributes', 'junctions', 'Road network junctions');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('segments', 'attributes', 'segments', 'Road segments within junctions');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('lanes', 'attributes', 'lanes', 'Driving lanes');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('lane_markings', 'attributes', 'lane_markings', 'Lane marking specifications');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('traffic_lights', 'attributes', 'traffic_lights', 'Traffic light positions and orientations');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('bulb_groups', 'attributes', 'bulb_groups', 'Traffic light bulb group assemblies');

INSERT INTO gpkg_contents (table_name, data_type, identifier, description)
VALUES ('bulbs', 'attributes', 'bulbs', 'Individual traffic light bulbs');

-- ============================================================================
-- EXAMPLE DATA: 2-LANE STRAIGHT ROAD
-- ============================================================================

-- Insert metadata
INSERT INTO maliput_metadata (key, value) VALUES ('linear_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('angular_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('scale_length', '1.0');
INSERT INTO maliput_metadata (key, value) VALUES ('inertial_to_backend_frame_translation', '{0.0, 0.0, 0.0}');

-- Insert junction
INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main Road Junction');

-- Insert segment
INSERT INTO segments (segment_id, junction_id, name) VALUES ('j1_s1', 'j1', 'Main Road Segment');

-- Insert boundaries (100m straight road, 3.5m lane width each)
INSERT INTO boundaries (boundary_id, geometry)
VALUES ('b_right', X'');  -- Placeholder: would contain BLOB-encoded LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)

INSERT INTO boundaries (boundary_id, geometry)
VALUES ('b_between', X'');  -- Placeholder: would contain BLOB-encoded LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)

INSERT INTO boundaries (boundary_id, geometry)
VALUES ('b_left', X'');  -- Placeholder: would contain BLOB-encoded LINESTRINGZ(0 7.0 0, 25 7.0 0, 50 7.0 0, 75 7.0 0, 100 7.0 0)

-- Insert lanes by referencing boundary IDs
-- Lane 1: y = 0 to y = 3.5
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
VALUES (
    'j1_s1_lane1',
    'j1_s1',
    'driving',
    'forward',
    'b_between',
    'b_right'
);

-- Lane 2: y = 3.5 to y = 7.0
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
VALUES (
    'j1_s1_lane2',
    'j1_s1',
    'driving',
    'forward',
    'b_left',
    'b_between'
);

-- Define branch points at start
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane1', 'a', 'start');

INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane2', 'a', 'start');

-- Define branch points at end
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane1', 'a', 'finish');

INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane2', 'a', 'finish');

-- Insert lane markings
-- White dashed line for center boundary (between lanes)
INSERT INTO lane_markings (marking_id, boundary_id, s_start, s_end, marking_type, color, weight, width, lane_change_rule)
VALUES (
    'marking_b_between',
    'b_between',
    0.0,
    100.0,
    'dashed',
    'white',
    'standard',
    0.15,
    'allowed'
);

-- Insert detailed line specification for the center marking
INSERT INTO lane_marking_lines (marking_id, line_index, length, space, width)
VALUES ('marking_b_between', 0, 3.0, 9.0, 0.15);

-- Yellow solid line for left edge (outer boundary)
INSERT INTO lane_markings (marking_id, boundary_id, s_start, s_end, marking_type, color, weight, width, lane_change_rule)
VALUES (
    'marking_b_left',
    'b_left',
    0.0,
    100.0,
    'solid',
    'yellow',
    'standard',
    0.15,
    'none'
);

-- Insert traffic light at intersection
INSERT INTO traffic_lights (traffic_light_id, inertial_x, inertial_y, inertial_z, yaw, name)
VALUES (
    'tl_intersection_1',
    50.0,
    10.0,
    4.5,
    0.0,
    'Intersection 1 - North Signal'
);

-- Insert bulb group for vehicle signals
INSERT INTO bulb_groups (bulb_group_id, traffic_light_id, relative_z, name)
VALUES (
    'bg_north_vehicles',
    'tl_intersection_1',
    0.0,
    'Vehicle Signal Group'
);

-- Insert bulbs (red, yellow, green stacked vertically)
INSERT INTO bulbs (bulb_id, bulb_group_id, relative_z, color, bulb_type)
VALUES ('bulb_red', 'bg_north_vehicles', 0.4, 'red', 'round');

INSERT INTO bulbs (bulb_id, bulb_group_id, relative_z, color, bulb_type)
VALUES ('bulb_yellow', 'bg_north_vehicles', 0.0, 'yellow', 'round');

INSERT INTO bulbs (bulb_id, bulb_group_id, relative_z, color, bulb_type)
VALUES ('bulb_green', 'bg_north_vehicles', -0.4, 'green', 'round');

-- Insert stop line at lane approach to intersection
-- Note: Placeholder BLOB; actual geometry would be GeoPackageBinary-encoded LINESTRINGZ(90 0 0, 90 3.5 0)
INSERT INTO stop_lines (stop_line_id, lane_id, s_position, geometry, traffic_light_id, name)
VALUES (
    'stop_line_j1_s1_lane1',
    'j1_s1_lane1',
    90.0,
    X'',  -- Placeholder for BLOB geometry
    'tl_intersection_1',
    'Stop Line - Intersection 1 Approach'
);

```

**Notes on GeoPackage Compliance:**

- **PRAGMA directives**: Set `application_id` and `user_version` at the file level to identify it as a GeoPackage.
- **Required core tables**: `gpkg_spatial_ref_sys`, `gpkg_contents`, and `gpkg_geometry_columns` are mandatory for GeoPackage compliance.
- **Geometry storage**: The example uses placeholder `X''` BLOBs for geometry columns. In actual use:
  - Convert WKT geometries (e.g., `LINESTRINGZ(0 0 0, 100 0 0)`) to GeoPackageBinary BLOB format using tools like GDAL, QGIS, or a spatial library.
  - Use standard GeoPackage geometry encoding to ensure interoperability with other GIS tools.
- **Integer primary keys**: All spatial feature tables (`boundaries`, `stop_lines`) use auto-incrementing integer `id` as their primary key, with human-friendly unique identifier columns (`boundary_id`, `stop_line_id`) for readability.
- **Spatial reference system**: The example uses EPSG:4326 (WGS 84). Adjust `srs_id` values if you use a different coordinate system.
- **Non-spatial tables**: Attribute tables (metadata, junctions, segments, lanes, markings, traffic control) are registered in `gpkg_contents` with `data_type='attributes'` so GIS tools recognize them as valid GeoPackage content without geometry requirements.

---

## Visual Representation

```text
                    y = 7.0
    ┌────────────────────────────────────────┐
    │              Lane 2                    │
    │  (j1_s1_lane2)                         │
    │                                        │
    ├────────────────────────────────────────┤ y = 3.5 (shared boundary)
    │              Lane 1                    │
    │  (j1_s1_lane1)                         │
    │                                        │
    └────────────────────────────────────────┘
   x=0                                      x=100
                    y = 0

   Direction: ─────────────────────────────────>
              (forward along positive x-axis)
```

---
