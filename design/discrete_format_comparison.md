# Sampled-Based Maliput Backend Format Analysis

This document consolidates the analysis of potential formats for creating a new sampled-based maliput backend, similar to `maliput_osm` but using a different underlying format.

## Context

### Current Maliput Backends

| Backend | Format Type | Description |
|---------|-------------|-------------|
| **maliput_malidrive** | Analytical | OpenDRIVE format with mathematical road descriptions |
| **maliput_osm** | Sampled | OSM/Lanelet2 format using `maliput_sparse` |

### Goal

Create a new backend based on a sampled/waypoint format that:
- Uses `maliput_sparse` for geometric calculations
- Targets a different user base or use case than `maliput_osm`
- Provides access to existing map data (if available)

---

## Format Candidates

### 1. CommonRoad

**Description:** Academic standard from TU Munich designed for motion planning research.

**Format Structure (XML):**
```xml
<lanelet id="1">
  <leftBound>
    <point><x>0.0</x><y>0.0</y><z>0.0</z></point>
    <point><x>10.0</x><y>0.5</y><z>0.0</z></point>
  </leftBound>
  <rightBound>
    <point><x>0.0</x><y>3.5</y><z>0.0</z></point>
    <point><x>10.0</x><y>4.0</y><z>0.0</z></point>
  </rightBound>
  <predecessor ref="0"/>
  <successor ref="2"/>
</lanelet>
```

**Pros:**
- Purpose-built for motion planning research
- XML-based, well-documented
- Growing benchmark dataset at [commonroad.in.tum.de](https://commonroad.in.tum.de/)
- Includes traffic scenarios
- Explicitly sampled (polylines)
- Has Lanelet2↔CommonRoad converters

**Cons:**
- Smaller ecosystem than Lanelet2
- More research-focused than production

---

### 2. nuScenes Map Format

**Description:** Map format from Motional's nuScenes dataset, widely used in perception/prediction research.

**Format Structure (JSON):**
```json
{
  "lane": [
    {
      "token": "abc123",
      "exterior_node_tokens": ["node1", "node2", "node3"],
      "lane_type": "CAR"
    }
  ],
  "node": [
    {"token": "node1", "x": 1000.0, "y": 2000.0}
  ],
  "lane_connector": [...],
  "road_segment": [...],
  "drivable_area": [...]
}
```

**Pros:**
- JSON-based (human-readable, easy to parse)
- Real-world data (Boston, Singapore)
- Free for research
- Very popular in ML community (prediction, planning papers)
- Includes drivable areas, pedestrian crossings, stop lines
- Well-documented Python SDK (`nuscenes-devkit`)

**Cons:**
- Node-based indirection (lanes reference node tokens, not inline points)
- Limited to nuScenes geographic areas
- No elevation data (2D only in practice)
- Research license only

---

### 3. Waymo Open Dataset Map Format

**Description:** Waymo's proprietary HD-map format used in their Open Dataset for autonomous driving research.

**Format Structure (Protocol Buffers):**
```protobuf
message Map {
  repeated MapFeature map_features = 1;
  repeated DynamicState dynamic_states = 2;
}
message LaneCenter {
  repeated MapPoint polyline = 1;
  LaneType type = 2;
  repeated BoundarySegment left_boundaries = 3;
  repeated BoundarySegment right_boundaries = 4;
}
```

**Pros:**
- High-quality, real-world data (San Francisco, Phoenix, etc.)
- Rich semantics (lane types, boundaries, crosswalks, speed bumps)
- Free for research use
- Growing adoption in ML/planning research
- Protocol Buffers = efficient parsing
- Includes dynamic states (traffic lights)

**Cons:**
- Protobuf dependency (not human-readable)
- Waymo-specific, not an industry standard
- Limited to Waymo's data (can't easily create your own maps)
- License restrictions for commercial use

---

### 4. GeoJSON (+ Custom Schema)

**Description:** Universal JSON-based format for geographic data. Requires defining a custom schema for HD-map semantics.

**Format Structure:**
```json
{
  "type": "FeatureCollection",
  "metadata": {
    "format": "maliput_geojson",
    "version": "1.0"
  },
  "features": [
    {
      "type": "Feature",
      "properties": {
        "feature_type": "lane",
        "lane_id": "1_0_1",
        "segment_id": "1_0",
        "junction_id": "1",
        "lane_type": "driving"
      },
      "geometry": {
        "type": "MultiLineString",
        "coordinates": [
          [[0.0, 1.75, 0.0], [50.0, 1.80, 0.1], [100.0, 1.75, 0.0]],
          [[0.0, 5.25, 0.0], [50.0, 5.30, 0.1], [100.0, 5.25, 0.0]]
        ]
      }
    }
  ]
}
```

**Pros:**
- Universal, simple format
- Easy to create/edit (QGIS, any text editor)
- No special tooling needed
- Great for custom/synthetic roads
- Huge ecosystem

**Cons:**
- No predefined HD-map schema (must define your own)
- Less semantic richness (needs YAML supplements)
- No existing HD-map datasets
- No spatial indexing

---

### 5. GeoPackage (+ Custom Schema)

**Description:** OGC standard for geospatial data storage, SQLite-based container format.

**Format Structure (SQL):**
```sql
CREATE TABLE lanes (
    fid INTEGER PRIMARY KEY,
    lane_id TEXT NOT NULL,
    segment_id TEXT NOT NULL,
    junction_id TEXT NOT NULL,
    lane_type TEXT CHECK(lane_type IN ('driving', 'shoulder', 'parking')),
    left_boundary LINESTRING NOT NULL,
    right_boundary LINESTRING NOT NULL,
    UNIQUE(lane_id)
);

CREATE TABLE branch_points (
    fid INTEGER PRIMARY KEY,
    branch_point_id TEXT NOT NULL,
    location POINT NOT NULL
);

CREATE TABLE branch_point_connections (
    fid INTEGER PRIMARY KEY,
    branch_point_id TEXT REFERENCES branch_points(branch_point_id),
    lane_id TEXT REFERENCES lanes(lane_id),
    side TEXT CHECK(side IN ('a', 'b')),
    lane_end TEXT CHECK(lane_end IN ('start', 'finish'))
);
```

**Pros:**
- OGC open standard (widely adopted in GIS)
- SQLite-based = queryable, indexed, efficient
- Built-in spatial indexing (R-tree)
- Supports any geometry type (2D, 3D, curves)
- Excellent tooling (QGIS, GDAL, PostGIS)
- Self-contained single file
- Extensible (custom tables/attributes)
- Schema enforcement at database level

**Cons:**
- No predefined HD-map schema (you define your own)
- Binary format (need tools to inspect)
- Requires SpatiaLite dependency for full functionality

---

### 6. Lanelet2/OSM

**Description:** Purpose-built HD-map format for autonomous driving, based on the OSM data model. Developed by FZI and adopted by the Autoware ecosystem.

**Format Structure (OSM XML):**
```xml
<osm>
  <node id="1" lat="49.0" lon="8.4">
    <tag k="ele" v="0.0"/>
  </node>
  <node id="2" lat="49.0001" lon="8.4001">
    <tag k="ele" v="0.1"/>
  </node>
  <way id="100">
    <nd ref="1"/><nd ref="2"/>
    <tag k="type" v="line_thin" />
    <tag k="subtype" v="solid" />
  </way>
  <relation id="200">
    <member type="way" ref="100" role="left"/>
    <member type="way" ref="101" role="right"/>
    <tag k="type" v="lanelet"/>
    <tag k="subtype" v="road"/>
    <tag k="speed_limit" v="50"/>
  </relation>
</osm>
```

**Pros:**
- Purpose-built for autonomous driving HD maps
- Rich semantic information (traffic rules, regulatory elements, speed limits, right-of-way)
- Active community (FZI, Autoware)
- Explicit left/right boundary polylines (sampled representation)
- Built-in topology through lanelet relations (predecessor/successor, adjacency)
- 3D support via elevation tags on nodes
- Extensible tagging system (any attribute can be added via key-value tags)
- Open-source C++ library with Python bindings
- Integrates with JOSM editor for map creation and editing
- Well-documented specification and growing adoption in the AV industry

**Cons:**
- OSM XML format is verbose (node/way/relation indirection)
- No spatial indexing (must load entire map into memory)
- No SQL query support
- Coordinate system based on lat/lon (requires projection for local Cartesian use)
- Tagging conventions can be inconsistent across different map providers
- Limited existing HD-map datasets compared to nuScenes/Waymo
- Parser complexity due to OSM data model (nodes → ways → relations)

---

## Complete Comparison Tables

### Format Characteristics

| Criteria | CommonRoad | nuScenes | Waymo | GeoJSON | GeoPackage | Lanelet2/OSM |
|----------|------------|----------|-------|---------|------------|--------------|
| **Format Type** | XML | JSON | Protobuf | JSON | SQLite | XML |
| **Schema Defined** | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No (custom needed) | ❌ No (custom needed) | ✅ Yes |
| **Human Readable** | ✅ Yes | ✅ Yes | ❌ No | ✅ Yes | ❌ No | ✅ Yes |
| **Z-Coordinate (Elevation)** | ❌ No (2D only) | ❌ No (2D only) | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| **Spatial Indexing** | ❌ No | ❌ No | ❌ No | ❌ No | ✅ Yes (R-tree) | ❌ No |
| **SQL Queries** | ❌ No | ❌ No | ❌ No | ❌ No | ✅ Yes | ❌ No |
| **Partial Loading** | ❌ No | ❌ No | ❌ No | ❌ No | ✅ Yes | ❌ No |
| **File Size** | Medium | Medium | Compact | Large | Compact | Medium |
| **Single File** | ✅ Yes | ❌ No (multi-table) | ✅ Yes | ❌ No (multiple) | ✅ Yes | ✅ Yes |

### Data & Ecosystem

### Data & Ecosystem

| Criteria | CommonRoad | nuScenes | Waymo | GeoJSON | GeoPackage | Lanelet2/OSM |
|----------|------------|----------|-------|---------|------------|--------------|
| **Existing Map Data** | ✅ Benchmarks | ✅ Boston/Singapore | ✅ SF/Phoenix | ❌ Create own | ❌ Create own | ✅ Limited |
| **Community Size** | Medium (academic) | Large (ML) | Growing (ML) | Huge (universal) | Large (GIS) | Medium (AV) |
| **Primary Users** | Motion planning researchers | Perception/prediction ML | AV researchers | GIS professionals | GIS professionals | Autoware/AV |
| **Tooling** | Python SDK | Python SDK | TF tools | Any GIS tool | QGIS/GDAL | JOSM/Lanelet2 |
| **License** | BSD 3-Clause | CC BY-NC-SA 4.0 | Custom (research) | Open Standard | OGC Standard | BSD / ODbL |
| **Private Use** | ✅ Yes | ✅ Yes | ✅ Yes (research) | ✅ Yes | ✅ Yes | ✅ Yes |
| **Commercial Use** | ✅ Yes | ❌ No | ❌ No | ✅ Yes | ✅ Yes | ✅ Yes |

### Implementation Complexity

| Criteria | CommonRoad | nuScenes | Waymo | GeoJSON | GeoPackage | Lanelet2/OSM |
|----------|------------|----------|-------|---------|------------|--------------|
| **Parser Complexity** | Low | Medium | Medium | Low | Medium | Medium |
| **Dependencies** | XML parser | JSON parser | Protobuf | JSON parser | SQLite + SpatiaLite | OSM parser |

### Lane Boundary Representation

| Criteria | CommonRoad | nuScenes | Waymo | GeoJSON | GeoPackage | Lanelet2/OSM |
|----------|------------|----------|-------|---------|------------|--------------|
| **Boundary Storage** | Explicit L/R polylines | Centerline + width | Explicit L/R | Custom schema | Custom schema | Explicit L/R |
| **3D Support (Elevation)** | ❌ No (2D only) | ❌ No (2D only) | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| **Topology Built-in** | ✅ Yes (predecessors/successors) | ✅ Yes (lane connectors) | ✅ Yes | ❌ Custom needed | ❌ Custom needed | ✅ Yes |

### Scalability

| Criteria | CommonRoad | nuScenes | Waymo | GeoJSON | GeoPackage | Lanelet2/OSM |
|----------|------------|----------|-------|---------|------------|--------------|
| **Small Maps (<1K lanes)** | ✅ Good | ✅ Good | ✅ Good | ✅ Good | ✅ Good | ✅ Good |
| **Medium Maps (1K-10K)** | ✅ Good | ✅ Good | ✅ Good | ⚠️ Slower | ✅ Good | ✅ Good |
| **Large Maps (>10K lanes)** | ⚠️ Load all | ⚠️ Load all | ⚠️ Load all | ❌ Poor | ✅ Excellent | ⚠️ Load all |
| **Region Queries** | ❌ Manual | ❌ Manual | ❌ Manual | ❌ Manual | ✅ Native SQL | ❌ Manual |


---

## Technical Deep Dives

### Spatial Indexing (GeoPackage Advantage)

**The Problem Without Spatial Indexing:**
```cpp
// Without index: O(n) - must check EVERY lane
std::vector<Lane> FindLanesNearPoint(Point p, double radius) {
    std::vector<Lane> result;
    for (const auto& lane : all_lanes) {  // 100,000 lanes? Check all 100,000
        if (lane.DistanceTo(p) < radius) {
            result.push_back(lane);
        }
    }
    return result;
}
```

**The Solution (R-Tree Index):**
GeoPackage uses an R-tree spatial index that enables O(log n) queries instead of O(n).

**Performance Comparison:**

| Map Size | Without Index | With Index |
|----------|---------------|------------|
| 1,000 lanes | 1ms | 0.1ms |
| 100,000 lanes | 100ms | 0.2ms |
| 1,000,000 lanes | 1,000ms | 0.3ms |

### GeoJSON Schema Requirements

If using GeoJSON, you need to define a custom schema. Example approach using MultiLineString for boundaries:

```json
{
  "type": "FeatureCollection",
  "metadata": {
    "format": "maliput_geojson",
    "version": "1.0",
    "linear_tolerance": 0.01,
    "angular_tolerance": 0.01
  },
  "features": [
    {
      "type": "Feature",
      "properties": {
        "feature_type": "lane",
        "id": {
          "lane_id": "1",
          "segment_id": "1",
          "junction_id": "1"
        },
        "lane_type": "driving",
        "boundary_types": {
          "left": "solid_white",
          "right": "dashed_white"
        }
      },
      "geometry": {
        "type": "MultiLineString",
        "coordinates": [
          [[0.0, 1.75, 0.0], [50.0, 1.80, 0.1], [100.0, 1.75, 0.0]],
          [[0.0, 5.25, 0.0], [50.0, 5.30, 0.1], [100.0, 5.25, 0.0]]
        ]
      }
    },
    {
      "type": "Feature",
      "properties": {
        "feature_type": "branch_point",
        "id": "bp_1",
        "a_side": [{"lane_id": "1", "end": "finish"}],
        "b_side": [{"lane_id": "2", "end": "start"}]
      },
      "geometry": {
        "type": "Point",
        "coordinates": [100.0, 3.5, 0.0]
      }
    }
  ]
}
```

**Convention:** 
- First LineString in MultiLineString = left boundary
- Second LineString = right boundary

### Mapping to maliput_sparse Builder

```cpp
// Example: Parsing GeoJSON to maliput_sparse
void ParseLaneFeature(const json& feature, maliput_sparse::builder::Builder& builder) {
  const auto& props = feature["properties"];
  const auto& coords = feature["geometry"]["coordinates"];
  
  // Extract boundaries (MultiLineString convention)
  std::vector<maliput::math::Vector3> left_boundary;
  std::vector<maliput::math::Vector3> right_boundary;
  
  for (const auto& pt : coords[0]) {  // First = left
    left_boundary.emplace_back(pt[0], pt[1], pt[2]);
  }
  for (const auto& pt : coords[1]) {  // Second = right
    right_boundary.emplace_back(pt[0], pt[1], pt[2]);
  }
  
  // Feed to maliput_sparse builder
  builder.AddLane(
    maliput::api::LaneId(props["id"]["lane_id"]),
    {left_boundary, right_boundary}
  );
}
```

---

## References

- [CommonRoad](https://commonroad.in.tum.de/)
- [nuScenes](https://www.nuscenes.org/)
- [Waymo Open Dataset](https://waymo.com/open/)
- [GeoJSON Specification](https://geojson.org/)
- [GeoPackage Specification](https://www.geopackage.org/)
- [Lanelet2](https://github.com/fzi-forschungszentrum-informatik/Lanelet2)
- [maliput_sparse](https://github.com/maliput/maliput_sparse)
- [maliput_osm](https://github.com/maliput/maliput_osm)