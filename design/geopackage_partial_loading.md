# Partial Loading in GeoPackage

This document analyzes GeoPackage's capabilities for partial/incremental loading of road network data, which is a key advantage over other HD-map formats.

## Overview

**Partial loading** refers to the ability to load only a subset of the road network data without reading the entire file. This is critical for:

- Large-scale maps (city-wide, regional, or national road networks)
- Memory-constrained systems (embedded devices, edge computing)
- Real-time applications (load data as vehicle moves)
- Faster startup times (load only what's needed immediately)

## Why GeoPackage Excels at Partial Loading

GeoPackage is built on **SQLite**, which provides:

| Feature | Benefit for Partial Loading |
|---------|----------------------------|
| **B-tree indexing** | O(log n) lookups by ID |
| **R-tree spatial indexing** | O(log n) geographic queries |
| **Page-based storage** | Only reads required disk pages |
| **SQL queries** | Flexible filtering at database level |
| **No full parse required** | Unlike XML/JSON, don't need to read entire file |

## Partial Loading Strategies

### 1. Load by Junction ID

Load a specific junction and all its contents:

```sql
-- Load a specific junction
SELECT * FROM junctions WHERE junction_id = 'intersection_main_oak';

-- Load all segments in that junction
SELECT * FROM segments WHERE junction_id = 'intersection_main_oak';

-- Load all lanes in those segments
SELECT l.* FROM lanes l
JOIN segments s ON l.segment_id = s.segment_id
WHERE s.junction_id = 'intersection_main_oak';
```

**Use case:** Loading a known intersection or highway section.

---

### 2. Load by Bounding Box (Spatial Query)

Load all lanes within a geographic region:

```sql
-- With SpatiaLite extension and the new `boundaries` table
SELECT l.* FROM lanes l
JOIN boundaries b ON l.left_boundary_id = b.boundary_id
WHERE MbrIntersects(
    GeomFromText(b.geometry),
    BuildMbr(min_x, min_y, max_x, max_y)
);
```

**Pure SQLite approach (without SpatiaLite):**

```sql
-- If you store bounding box columns
SELECT * FROM lanes
WHERE bbox_min_x <= :query_max_x 
  AND bbox_max_x >= :query_min_x
  AND bbox_min_y <= :query_max_y
  AND bbox_max_y >= :query_min_y;
```

**Use case:** Load roads around the vehicle's current position.

---

### 3. Load by Lane Count

For memory-constrained systems, limit the number of lanes:

```sql
-- Load only the first 1000 lanes
SELECT * FROM lanes LIMIT 1000;

-- Load lanes in batches (pagination)
SELECT * FROM lanes ORDER BY lane_id LIMIT 100 OFFSET 0;   -- Batch 1
SELECT * FROM lanes ORDER BY lane_id LIMIT 100 OFFSET 100; -- Batch 2
```

**Use case:** Progressive loading on low-memory devices.

---

### 4. Load by Road Type

Load only specific types of roads:

```sql
-- Load only driving lanes (skip shoulders, parking)
SELECT * FROM lanes WHERE lane_type = 'driving';

-- Load only highway lanes (if you add a road_class column)
SELECT l.* FROM lanes l
JOIN segments s ON l.segment_id = s.segment_id
JOIN junctions j ON s.junction_id = j.junction_id
WHERE j.road_class = 'highway';
```

**Use case:** Highway-only navigation mode.

---

### 5. Load Connected Lanes (Graph Traversal)

Start from one lane and load connected neighbors:

```sql
-- Load a starting lane
SELECT * FROM lanes WHERE lane_id = 'lane_start';

-- Load lanes connected at branch points
SELECT DISTINCT l.* FROM lanes l
JOIN branch_point_lanes bpl1 ON l.lane_id = bpl1.lane_id
JOIN branch_point_lanes bpl2 ON bpl1.branch_point_id = bpl2.branch_point_id
WHERE bpl2.lane_id = 'lane_start' AND l.lane_id != 'lane_start';

-- Load adjacent lanes
SELECT l.* FROM lanes l
JOIN adjacent_lanes al ON l.lane_id = al.adjacent_lane_id
WHERE al.lane_id = 'lane_start';
```

**Use case:** Load only the route corridor for navigation.

---

## Handling Missing Connections

When doing partial loading, the `branch_point_lanes` and `adjacent_lanes` tables may reference lanes that **weren't loaded**. This causes problems during RoadNetwork construction.

### The Problem

```
Loaded Region                    Not Loaded
┌─────────────────┐              ┌─────────────────┐
│  Lane A ────────┼──────────────┼──► Lane X       │
│                 │  Branch      │    (not in      │
│  Lane B ◄───────┼──────────────┼─── memory)      │
│                 │  Point       │                 │
└─────────────────┘              └─────────────────┘
```

The `branch_point_lanes` table contains:
```
| branch_point_id | lane_id | end  |
|-----------------|---------|------|
| bp_1            | lane_A  | end  |
| bp_1            | lane_X  | start|  <-- Lane X not loaded!
```

When building the RoadNetwork, processing `bp_1` will fail because `lane_X` doesn't exist in the loaded data. This is a **dangling reference** that breaks construction.

### Solutions

#### 1. Filter Branch Points During Query

Only load branch point entries where **all referenced lanes are loaded**:

```sql
-- First: Get IDs of loaded lanes
-- (from your spatial/filter query)

-- Then: Only load branch points where ALL lanes are in loaded set
SELECT bpl.* FROM branch_point_lanes bpl
WHERE bpl.branch_point_id IN (
    SELECT branch_point_id FROM branch_point_lanes
    GROUP BY branch_point_id
    HAVING COUNT(*) = COUNT(
        CASE WHEN lane_id IN (... loaded lane IDs ...) THEN 1 END
    )
);
```

**Trade-off:** Boundary lanes will have NO branch points (not even partial ones), appearing as completely disconnected dead-ends.

#### 2. Filter at Build Time

Load all branch point data, but skip entries with missing lanes:

```cpp
// When processing branch_point_lanes from database
for (const auto& bp_entry : branch_point_lanes_data) {
    if (!loaded_lanes_.contains(bp_entry.lane_id)) {
        // Skip this entry - lane not loaded
        // Log warning for debugging
        spdlog::warn("Skipping branch point entry: lane '{}' not loaded", 
                     bp_entry.lane_id);
        continue;
    }
    // Process normally
    branch_points_[bp_entry.branch_point_id].AddLane(
        loaded_lanes_[bp_entry.lane_id], bp_entry.end);
}
```

**Trade-off:** Branch points at boundaries will have fewer lanes than expected. A branch point might end up with only one lane (which is valid in maliput - it's just a dead-end).

#### 3. Expand Query to Include All Connected Lanes

Ensure no dangling references by loading ALL lanes that share a branch point with your region:

```sql
-- Step 1: Load lanes in bbox
CREATE TEMP TABLE loaded_lanes AS
SELECT lane_id FROM lanes WHERE bbox_min_x <= ? AND bbox_max_x >= ? ...;

-- Step 2: Find all branch points touching those lanes
CREATE TEMP TABLE touched_branch_points AS
SELECT DISTINCT branch_point_id FROM branch_point_lanes
WHERE lane_id IN (SELECT lane_id FROM loaded_lanes);

-- Step 3: Load ALL lanes connected to those branch points
SELECT l.* FROM lanes l
WHERE l.lane_id IN (
    SELECT DISTINCT lane_id FROM branch_point_lanes
    WHERE branch_point_id IN (SELECT branch_point_id FROM touched_branch_points)
);
```

**Trade-off:** May load significantly more lanes than the original bbox, but guarantees no dangling references.

#### 4. Same Approach for Adjacent Lanes

```sql
-- Filter adjacent_lanes to only include loaded pairs
SELECT * FROM adjacent_lanes
WHERE lane_id IN (... loaded lane IDs ...)
  AND adjacent_lane_id IN (... loaded lane IDs ...);
```

### Recommended Implementation

Since maliput's `RoadNetwork` is **immutable after construction**, the partial loading strategy must account for this:

#### Option A: Load Complete Connectivity at Build Time

Ensure all referenced lanes are loaded before building the RoadNetwork:

```cpp
LoadResult LoadRegion(double min_x, double min_y, double max_x, double max_y) {
    // 1. Load lanes in bbox
    auto lanes = QueryLanesInBbox(min_x, min_y, max_x, max_y);
    
    // 2. Load branch points for those lanes
    auto branch_points = QueryBranchPointsForLanes(lanes);
    
    // 3. Expand to include all connected lanes (closure)
    std::set<std::string> loaded_ids = GetLaneIds(lanes);
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const auto& bp : branch_points) {
            for (const auto& connected_id : bp.lane_ids) {
                if (!loaded_ids.contains(connected_id)) {
                    // Load the missing lane
                    lanes.push_back(QueryLaneById(connected_id));
                    loaded_ids.insert(connected_id);
                    expanded = true;
                }
            }
        }
        // Re-query branch points for newly added lanes
        if (expanded) {
            branch_points = QueryBranchPointsForLanes(lanes);
        }
    }
    
    // 4. Build complete RoadNetwork with all connections resolved
    return BuildRoadNetwork(lanes, branch_points);
}
```

**Trade-off:** May load more lanes than expected if connectivity spans large areas.

#### Option B: Truncate at Boundary (Dead-End Lanes)

Accept that boundary lanes will have incomplete connections:

```cpp
// When building branch points, only include loaded lanes
// Boundary lanes simply won't have connections on one side
// This is valid - maliput supports lanes with empty branch points

for (const auto& bp_data : branch_points) {
    std::vector<Lane*> a_side, b_side;
    for (const auto& lane_id : bp_data.a_side_lanes) {
        if (loaded_lanes_.contains(lane_id)) {
            a_side.push_back(loaded_lanes_[lane_id]);
        }
        // Unloaded lanes are simply not added
    }
    // Build branch point with whatever lanes are available
}
```

**Trade-off:** Boundary lanes appear as dead-ends, which may affect routing algorithms.

---

## Schema Enhancements for Partial Loading

### Add Bounding Box Columns

For fast spatial queries without SpatiaLite:

```sql
ALTER TABLE lanes ADD COLUMN bbox_min_x REAL;
ALTER TABLE lanes ADD COLUMN bbox_max_x REAL;
ALTER TABLE lanes ADD COLUMN bbox_min_y REAL;
ALTER TABLE lanes ADD COLUMN bbox_max_y REAL;

-- Create index for spatial queries
CREATE INDEX idx_lanes_bbox ON lanes(bbox_min_x, bbox_max_x, bbox_min_y, bbox_max_y);
```

### Add Tile/Grid Reference

For tile-based loading (like map tiles):

```sql
ALTER TABLE lanes ADD COLUMN tile_id TEXT;

-- Load all lanes in a specific tile
SELECT * FROM lanes WHERE tile_id = 'tile_12_45';

-- Create index
CREATE INDEX idx_lanes_tile ON lanes(tile_id);
```

### Add Road Hierarchy

For loading by importance:

```sql
ALTER TABLE junctions ADD COLUMN road_class TEXT
    CHECK (road_class IN ('highway', 'arterial', 'collector', 'local'));

-- Load highways first, then progressively add detail
SELECT * FROM junctions WHERE road_class = 'highway';
SELECT * FROM junctions WHERE road_class IN ('highway', 'arterial');
```

---

## Summary

GeoPackage's partial loading capability is a **significant advantage** for large-scale road networks:

| Benefit | Impact |
|---------|--------|
| **Reduced memory** | Load only needed data |
| **Faster startup** | Don't parse entire file |
| **Scalability** | Handle city/country-scale maps |
| **Flexibility** | Multiple query strategies |
| **Real-time loading** | Load data as vehicle moves |
