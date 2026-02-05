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
    - [Core Tables](#core-tables)
      - [`maliput_metadata`](#maliput_metadata)
      - [`junctions`](#junctions)
      - [`segments`](#segments)
      - [`boundaries`](#boundaries)
      - [`lanes`](#lanes)
    - [Connectivity Tables](#connectivity-tables)
      - [`branch_point_lanes`](#branch_point_lanes)
      - [`adjacent_lanes`](#adjacent_lanes)
  - [Complete Example](#complete-example)
  - [Visual Representation](#visual-representation)

## Schema Definition

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

Stores shared boundary geometries as WKT LINESTRINGZ. These are referenced from `lanes` by ID to avoid duplicating identical boundary geometry when adjacent lanes share a common edge.

```sql
CREATE TABLE IF NOT EXISTS boundaries (
    boundary_id TEXT PRIMARY KEY,
    geometry TEXT NOT NULL  -- WKT LINESTRINGZ(x1 y1 z1, x2 y2 z2, ...)
);
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `boundary_id` | TEXT | Unique identifier for the boundary |
| `geometry` | TEXT | WKT LINESTRINGZ geometry string |

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
```

| Column | Type | Description |
| -------- | ------ | ------------- |
| `lane_id` | TEXT | Unique identifier for the lane |
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

#### `adjacent_lanes`

Defines lateral adjacency between parallel lanes in the same segment.

Adjacency between lanes is derivable from shared `boundaries` references. When two lanes share a common boundary such that one lane's `right_boundary_id` equals the other's `left_boundary_id`, they are adjacent (the second lane is on the right of the first). Likewise, when one lane's `left_boundary_id` equals another's `right_boundary_id`, the second lane is on the left of the first.

Because adjacency can be computed from `lanes.left_boundary_id` / `lanes.right_boundary_id`, we avoid duplicating this data in a table. Instead provide a read-only SQL `VIEW` that derives the adjacency on demand:

```sql
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
```

This keeps the schema normalized and guarantees adjacency consistency without requiring maintenance logic.

---

## Complete Example

Here's a complete SQL script to create a simple 2-lane straight road:

```sql
-- Create tables
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

CREATE TABLE IF NOT EXISTS boundaries (
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

CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);

-- Adjacent lanes are derived from shared boundaries. Use a VIEW such as:
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

-- Insert metadata
INSERT INTO maliput_metadata (key, value) VALUES ('linear_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('angular_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('scale_length', '1.0');

-- Insert junction
INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main Road Junction');

-- Insert segment
INSERT INTO segments (segment_id, junction_id, name) VALUES ('j1_s1', 'j1', 'Main Road Segment');

-- Insert shared boundaries (100m straight road, 3.5m lane width each)
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_right', 'LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)');
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_between', 'LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)');
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_left', 'LINESTRINGZ(0 7.0 0, 25 7.0 0, 50 7.0 0, 75 7.0 0, 100 7.0 0)');

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

-- Define branch points
-- Start branch point
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane1', 'a', 'start');
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane2', 'a', 'start');

-- End branch point
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane1', 'a', 'finish');
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane2', 'a', 'finish');

-- Define lane adjacency
-- Adjacency is derived; no manual INSERTs are necessary when using the view above.
```

---

## Visual Representation

```text
                    y = 7.0
    ┌────────────────────────────────────────┐
    │              Lane 2                     │
    │  (j1_s1_lane2)                         │
    │                                        │
    ├────────────────────────────────────────┤ y = 3.5 (shared boundary)
    │              Lane 1                     │
    │  (j1_s1_lane1)                         │
    │                                        │
    └────────────────────────────────────────┘
   x=0                                      x=100
                    y = 0

   Direction: ─────────────────────────────────>
              (forward along positive x-axis)
```

---
