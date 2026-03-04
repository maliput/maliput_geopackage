# GitHub Copilot Onboarding Instructions for maliput_geopackage

This document provides coding agents with essential information to work efficiently on this repository without extensive exploration.

## Repository Overview

**maliput_geopackage** is a [maliput](https://github.com/maliput/maliput) backend implementation that loads road network data from [GeoPackage](https://www.geopackage.org/) (`.gpkg`) files. GeoPackage is an OGC standard format that uses SQLite as its container.

This backend relies on [maliput_sparse](https://github.com/maliput/maliput_sparse) for building the road geometry from sampled lane boundaries. Unlike `maliput_malidrive` (which uses analytical road curves from OpenDRIVE), this backend works with **discrete/sampled** lane boundary geometries stored as LINESTRING features in a GeoPackage database.

**Technologies:** C++17, CMake, Bazel, ROS 2 (ament), Google Test, SQLite3
**Documentation:** https://maliput.readthedocs.io/

### Key Capabilities

- Parses GeoPackage files containing HD-map road network data
- Stores lane boundaries as sampled 3D LINESTRING geometries
- Supports spatial indexing (R-tree) for efficient queries
- Single-file distribution compatible with GIS tools (QGIS, GDAL)
- Delegates road geometry construction to `maliput_sparse`
- Provides traffic light, phase ring, and intersection book support via YAML configuration
- SQL-based schema enabling partial loading strategies (by region, junction, etc.)

### Development Status

The parser layer (`GeoPackageParser`) is functional. However, `GeoPackageManager::DoGetJunctions()` and `DoGetConnections()` currently return empty results with TODO comments — junction and connection resolution is not yet implemented.

## Architecture: maliput Backend

```
┌─────────────────────────────────────────────────────────────┐
│                    Your Application                         │
│         (uses maliput::api interfaces)                      │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     maliput (API)                           │
│  - Abstract API (RoadGeometry, Lane, Segment, Junction)     │
│  - Plugin system for loading backends                       │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              maliput_sparse (Geometry Layer)                 │
│  - parser::Parser interface                                 │
│  - loader::RoadNetworkLoader                                │
│  - Builds road geometry from sampled lane boundaries        │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│            maliput_geopackage (this repo)                   │
│  - GeoPackage parser (geopackage/)                          │
│  - Builder (builder/)                                       │
│  - Plugin registration (plugin/)                            │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                GeoPackage File (.gpkg)                       │
│       (SQLite database with HD-map road network)            │
└─────────────────────────────────────────────────────────────┘
```

**Key architectural difference from `maliput_malidrive`:** This backend does **not** directly implement maliput's `Lane`, `Segment`, or `RoadGeometry`. Instead, it implements `maliput_sparse::parser::Parser` (via `GeoPackageManager`) and delegates road geometry construction entirely to `maliput_sparse::loader::RoadNetworkLoader`.

### Usage Patterns

**Direct usage via builder:**
```cpp
#include <maliput_geopackage/builder/road_network_builder.h>

const std::map<std::string, std::string> builder_config {
  {"gpkg_file", "/path/to/road_network.gpkg"},
  {"road_geometry_id", "my_road_network"},
  {"linear_tolerance", "0.01"},
  {"angular_tolerance", "0.01"},
};

auto road_network = maliput_geopackage::builder::RoadNetworkBuilder(builder_config)();
const maliput::api::RoadGeometry* rg = road_network->road_geometry();
```

**Plugin-based loading (preferred for applications):**
```cpp
#include <maliput/plugin/create_road_network.h>

auto road_network = maliput::plugin::CreateRoadNetwork(
    "maliput_geopackage",  // backend name
    {{"gpkg_file", "/path/to/road_network.gpkg"}}
);
```

## Build System & Commands

### Prerequisites

```bash
sudo apt install python3-rosdep python3-colcon-common-extensions libsqlite3-dev
```

### CMake/Colcon Build (Recommended)

```bash
# Create workspace
mkdir -p colcon_ws/src
cd colcon_ws/src
git clone https://github.com/maliput/maliput_geopackage.git

# Install dependencies
export ROS_DISTRO=foxy
rosdep update
rosdep install -i -y --rosdistro $ROS_DISTRO --from-paths src

# Build
colcon build --packages-up-to maliput_geopackage

# Build with documentation
colcon build --packages-select maliput_geopackage --cmake-args " -DBUILD_DOCS=On"
```

### Bazel Build

```bash
# Build all targets
bazel build //...

# Build specific targets
bazel build //:geopackage
bazel build //:builder
```

### Testing

```bash
# CMake/colcon
colcon test --packages-select maliput_geopackage
colcon test-result --verbose

# Bazel
bazel test //...
```

### Code Formatting

```bash
# Check format
ament_clang_format --config=./.clang-format

# Reformat code (if tools/reformat_code.sh exists)
./tools/reformat_code.sh
```

## Project Architecture

### Directory Structure

```
maliput_geopackage/
├── include/maliput_geopackage/           # Public headers
│   └── builder/
│       ├── params.h                      # Builder configuration parameter keys
│       └── road_network_builder.h        # Main RoadNetworkBuilder class
├── src/
│   ├── maliput_geopackage/
│   │   ├── builder/                      # Builder implementations
│   │   │   ├── builder_configuration.cc/h  # Config struct with FromMap()/ToStringMap()
│   │   │   └── road_network_builder.cc   # Builds RoadNetwork from gpkg
│   │   └── geopackage/                   # GeoPackage parser (internal/private)
│   │       ├── geopackage_parser.cc/h    # SQL-based parser for gpkg tables
│   │       ├── geopackage_manager.cc/h   # Implements maliput_sparse::parser::Parser
│   │       └── sqlite_helpers.cc/h       # RAII SQLite wrappers
│   └── plugin/
│       └── road_network.cc               # maliput plugin registration
├── test/                                 # Unit tests
│   ├── geopackage_parser_test.cc         # Parser tests with real + temp gpkg files
│   ├── sqlite_helpers_test.cc            # SQLite wrapper tests
│   ├── builder/
│   │   └── builder_configuration_test.cc # Configuration round-trip tests
│   └── resources/
│       └── two_lane_road.gpkg            # Test fixture GeoPackage file
├── schema/                               # GeoPackage schema specification
│   ├── README.md                         # Full schema documentation
│   ├── REVIEW.md                         # Schema compliance review
│   └── tools/
│       ├── schema.sql                    # Canonical SQL schema definition
│       ├── template.gpkg                 # Empty GeoPackage from schema
│       ├── create_four_lanes_road.py     # Example map creator
│       ├── verify_file.py                # GeoPackage file verification
│       ├── extract_submap.py             # Submap extraction tool
│       ├── spatial_queries_example.py    # Spatial query examples
│       └── spatial_sql_overview.md       # Spatial SQL documentation
├── design/                               # Design documents
│   ├── discrete_format_comparison.md     # Format comparison analysis
│   └── geopackage_partial_loading.md     # Partial loading strategies
├── resources/                            # Sample GeoPackage files
│   ├── two_lane_road.gpkg               # Simple 2-lane road sample
│   ├── complex_road.gpkg                # Complex road network sample
│   ├── create_two_lane_road.py          # Python script to create sample
│   └── create_complex_road.py           # Python script to create sample
├── env-hooks/                            # ROS 2 environment setup
├── cmake/                                # CMake configuration
│   ├── DefaultCFlags.cmake
│   └── SanitizersConfig.cmake
├── bazel/                                # Bazel configuration
│   └── variables.bzl                     # Bazel COPTS
├── BUILD.bazel                           # Root Bazel build
├── MODULE.bazel                          # Bazel module definition (bzlmod)
├── WORKSPACE.bazel
├── CMakeLists.txt                        # Root CMake
├── package.xml                           # ROS 2 package manifest
├── .clang-format                         # Clang-format configuration
└── sandbox/                              # Experimental scripts
```

### Key Components

| Component | Description |
|-----------|-------------|
| `geopackage/` | SQLite-based GeoPackage parser — reads `.gpkg` tables into internal data structures |
| `builder/` | Constructs maliput objects from parsed GeoPackage data via `maliput_sparse` |
| `plugin/` | maliput plugin system integration |
| `schema/` | GeoPackage schema specification, SQL definition, and Python tooling |

### Key Classes

| Class/Struct | Location | Role |
|---|---|---|
| `RoadNetworkBuilder` | `include/.../builder/road_network_builder.h` | Public entry point; builds `RoadNetwork` from config map |
| `BuilderConfiguration` | `src/.../builder/builder_configuration.h` | Config struct with `FromMap()`/`ToStringMap()` conversion |
| `GeoPackageManager` | `src/.../geopackage/geopackage_manager.h` | Implements `maliput_sparse::parser::Parser` interface |
| `GeoPackageParser` | `src/.../geopackage/geopackage_parser.h` | Parses all GeoPackage tables into internal structs |
| `GPKGJunction` | `src/.../geopackage/geopackage_parser.h` | Parsed junction data |
| `GPKGSegment` | `src/.../geopackage/geopackage_parser.h` | Parsed segment data |
| `GPKGLaneBoundary` | `src/.../geopackage/geopackage_parser.h` | Parsed boundary geometry (3D point vectors) |
| `GPKGLane` | `src/.../geopackage/geopackage_parser.h` | Parsed lane data |
| `GPKGBranchPointLane` | `src/.../geopackage/geopackage_parser.h` | Parsed branch point connectivity entry |
| `GPKGAdjacentLane` | `src/.../geopackage/geopackage_parser.h` | Parsed lateral adjacency entry |
| `SqliteDatabase` | `src/.../geopackage/sqlite_helpers.h` | RAII SQLite database connection (read-only) |
| `SqliteStatement` | `src/.../geopackage/sqlite_helpers.h` | RAII SQLite prepared statement wrapper |

### Build Flow

1. `RoadNetworkBuilder` receives a config map
2. Creates `BuilderConfiguration::FromMap(config)` to parse parameters
3. Creates `GeoPackageManager` with the `.gpkg` file path
4. `GeoPackageManager` internally creates a `GeoPackageParser` that reads all tables
5. Delegates to `maliput_sparse::loader::RoadNetworkLoader(parser, sparse_config)()`

## GeoPackage Schema

The schema (defined in `schema/tools/schema.sql`) is OGC GeoPackage 1.3 compliant. See `schema/README.md` for full documentation.

### Core Tables

| Table | Description |
|-------|-------------|
| `maliput_metadata` | Key-value configuration (tolerances, schema version, etc.) |
| `junctions` | Junction containers (junction_id, name) |
| `segments` | Segments within junctions (segment_id, junction_id, name) |
| `lane_boundaries` | Shared boundary geometries as GeoPackageBinary LINESTRING BLOBs |
| `lanes` | Lanes referencing left/right boundaries (with inversion flags) |

### Connectivity Tables

| Table | Description |
|-------|-------------|
| `branch_point_lanes` | Lane connectivity (branch_point_id, lane_id, side a/b, lane_end start/finish) |
| `view_adjacent_lanes` | SQL VIEW deriving lateral adjacency from shared boundaries |

### Road Markings Tables

| Table | Description |
|-------|-------------|
| `lane_markings` | Road marking metadata per boundary (type, color, s-coordinate range) |
| `lane_marking_lines` | Detailed marking line components |

### Traffic Control Tables

| Table | Description |
|-------|-------------|
| `traffic_lights` | Traffic light positions and orientations |
| `bulb_groups` | Bulb group collections within traffic lights |
| `bulbs` | Individual bulbs (color, type) within groups |

### Coordinate System

Uses a custom local Cartesian spatial reference system (SRS ID 100000) — no geodesy needed. This matches maliput's inertial frame directly.

### Geometry Storage

Geometries are stored as **GeoPackageBinary BLOBs** (not WKT text). The format consists of a GeoPackage header (magic "GP", version, flags, SRS, optional envelope) followed by a WKB-encoded geometry. The parser currently supports only LINESTRING (WKB type 2), little-endian byte order, with optional Z-coordinates.

### Schema Tools

Python tools in `schema/tools/`:

| Tool | Description |
|------|-------------|
| `verify_file.py` | Verify a `.gpkg` file conforms to the schema |
| `extract_submap.py` | Extract a submap from a larger GeoPackage |
| `create_four_lanes_road.py` | Example: create a 4-lane road GeoPackage |
| `spatial_queries_example.py` | Demonstrates spatial SQL queries |
| `template.gpkg` | Empty GeoPackage with schema pre-created |

## Builder Configuration Parameters

Key parameters for `RoadNetworkBuilder` (see `include/maliput_geopackage/builder/params.h`):

| Parameter | Description | Default |
|-----------|-------------|---------|
| `gpkg_file` | Path to GeoPackage file | Required (`""`) |
| `road_geometry_id` | ID for the RoadGeometry | `"maliput"` |
| `linear_tolerance` | Linear tolerance (meters) | `"5e-2"` |
| `angular_tolerance` | Angular tolerance (radians) | `"1e-3"` |
| `scale_length` | Scale length | `"1.0"` |
| `inertial_to_backend_frame_translation` | Frame translation `{X, Y, Z}` | `"{0., 0., 0.}"` |
| `road_rule_book` | Path to YAML road rulebook | `""` |
| `rule_registry` | Path to YAML rule registry | `""` |
| `traffic_light_book` | Path to YAML traffic light book | `""` |
| `phase_ring_book` | Path to YAML phase ring book | `""` |
| `intersection_book` | Path to YAML intersection book | `""` |

Most parameter keys are inherited from `maliput_sparse::loader::config`. The `gpkg_file` parameter is specific to this backend.

## CI/CD & Validation

### GitHub Workflows

Located in `.github/workflows/`:

| Workflow | Description |
|----------|-------------|
| `build.yml` | Main CI: CMake (colcon) + Bazel builds |
| `bazel.yml` | Bazel-only builds (6.x, 7.x, 8.x) |
| `build_macos.yaml` | macOS Bazel builds |
| `containers.yml` | Container image builds |
| `release.yaml` | Release workflow |

### Validation Checklist

Before submitting changes:

1. **Format (REQUIRED):**
   ```bash
   ament_clang_format --config=./.clang-format
   ```

2. **Build & Test:**
   ```bash
   colcon build --packages-select maliput_geopackage
   colcon test --packages-select maliput_geopackage
   colcon test-result --verbose
   ```

## Code Style Guidelines

### C++ Style

Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) with project-specific configurations.

**Key settings (from `.clang-format`):**
- **Line length:** 120 characters
- **Pointer alignment:** Left (`int* ptr` not `int *ptr`)
- **Include order:** Related header, C headers, C++ headers, library headers, project headers

### Error Handling

Uses **maliput's** error handling macros directly (no custom wrapper macros like `maliput_malidrive`):

```cpp
#include <maliput/common/maliput_abort.h>

// Validate conditions with descriptive messages (PREFERRED)
MALIPUT_VALIDATE(condition, "Descriptive error message");

// Throw with message
MALIPUT_THROW_MESSAGE("Something went wrong");
```

### Copy/Move Semantics

Use maliput's macros to explicitly declare copy/move behavior:

```cpp
#include <maliput/common/maliput_copyable.h>

class MyClass {
 public:
  // Delete all copy/move operations
  MALIPUT_NO_COPY_NO_MOVE_NO_ASSIGN(MyClass)
};
```

### Logging

Use maliput's logging system:

```cpp
#include <maliput/common/logger.h>

maliput::log()->trace("Trace message");
maliput::log()->debug("Debug message");
maliput::log()->info("Info message");
maliput::log()->warn("Warning message");
maliput::log()->error("Error message");
```

### Naming Conventions

- **Classes/Structs:** `PascalCase` (e.g., `GeoPackageParser`, `BuilderConfiguration`)
- **Functions/Methods:** `PascalCase` for public API (e.g., `GetMetadata()`, `ParseBoundaries()`)
- **Variables:** `snake_case` (e.g., `gpkg_file_path`, `lane_boundaries`)
- **Member variables:** `snake_case_` with trailing underscore (e.g., `parser_`, `db_`)
- **Constants:** `kPascalCase` (e.g., `kGpkgFile`, `kLinearTolerance`)
- **Namespaces:** `lowercase` (e.g., `maliput_geopackage::builder`, `maliput_geopackage::geopackage`)

### Header Guards

Use `#pragma once` (not traditional include guards).

### Documentation

Use Doxygen-style comments:

```cpp
/// Brief description of the function.
///
/// Detailed description if needed.
///
/// @param param_name Description of parameter.
/// @returns Description of return value.
/// @throws ExceptionType When this condition occurs.
```

## Testing Patterns

Tests use Google Test (gtest):

```cpp
#include <gtest/gtest.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {
namespace {

class GeoPackageParserTest : public ::testing::Test {
 protected:
  const std::string gpkg_file_{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};
};

TEST_F(GeoPackageParserTest, ParsesMetadata) {
  GeoPackageParser dut(gpkg_file_);
  const auto metadata = dut.GetMetadata();
  EXPECT_FALSE(metadata.empty());
}

}  // namespace
}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
```

### Testing Conventions

- **Test fixture:** `class TestName : public ::testing::Test`
- **Free tests:** `GTEST_TEST(...)` or `TEST_F(...)`
- **DUT pattern:** Use "dut" (Device Under Test) for the object being tested
- **Resources:** `TEST_RESOURCES_DIR` macro defined at compile time, pointing to `test/resources/`
- **Namespace:** `maliput_geopackage::<component>::test`
- **Temporary databases:** `TempGeoPackage` helper class in parser tests creates in-memory SQLite databases for edge-case testing (invalid geometry, missing tables, etc.)
- **Geometry blob helpers:** `BuildGeometryBlob()` constructs GeoPackageBinary BLOBs for test fixtures

Test files are located in `test/` mirroring the source structure.

## Resources

Sample GeoPackage maps are provided in `resources/`:
- `two_lane_road.gpkg` — Simple 2-lane road
- `complex_road.gpkg` — More complex road network

Python scripts to generate samples:
- `create_two_lane_road.py`
- `create_complex_road.py`

Environment variable `MALIPUT_GEOPACKAGE_RESOURCE_ROOT` points to installed resources.

## Common Gotchas

1. **Use `MALIPUT_VALIDATE` / `MALIPUT_THROW_MESSAGE`:** This repo uses maliput's macros directly, **not** `MALIDRIVE_VALIDATE` or other custom macros from `maliput_malidrive`
2. **Use `MALIPUT_NO_COPY_NO_MOVE_NO_ASSIGN`:** Not `MALIDRIVE_NO_COPY_NO_MOVE_NO_ASSIGN`
3. **Internal headers are private:** The `geopackage/` parser headers live in `src/`, not `include/` — they are not part of the public API
4. **GeoPackage geometry format:** Stored as GeoPackageBinary BLOBs (GP header + WKB), not WKT text. The parser only supports LINESTRING (WKB type 2)
5. **Coordinate system:** Uses local Cartesian SRS (srs_id=100000), not geodetic coordinates
6. **Include order matters:** Follow the order enforced by `.clang-format`
7. **Namespace wrapping:** Always wrap code in `namespace maliput_geopackage { ... }`
8. **Junction/connection resolution:** `GeoPackageManager::DoGetJunctions()` and `DoGetConnections()` are not yet implemented — they return empty results
9. **const correctness:** Use `const` wherever possible

## Related Projects

- [maliput](https://github.com/maliput/maliput) — Core API (dependency)
- [maliput_sparse](https://github.com/maliput/maliput_sparse) — Sampled geometry layer (dependency)
- [maliput_malidrive](https://github.com/maliput/maliput_malidrive) — OpenDRIVE analytical backend
- [maliput_multilane](https://github.com/maliput/maliput_multilane) — Procedural road backend
- [maliput_dragway](https://github.com/maliput/maliput_dragway) — Simple straight roads
- [maliput_osm](https://github.com/maliput/maliput_osm) — OpenStreetMap backend

## Design Documents

- **`design/discrete_format_comparison.md`** — Compares 6 formats (CommonRoad, nuScenes, Waymo, GeoJSON, GeoPackage, Lanelet2/OSM) for a sampled-based maliput backend. Explains why GeoPackage was chosen.
- **`design/geopackage_partial_loading.md`** — Analyzes partial loading strategies: by junction ID, bounding box, lane count, road type, and graph traversal. Discusses dangling reference handling.

## Documentation Links

- **Main Documentation:** https://maliput.readthedocs.io/
- **Schema Reference:** See `schema/README.md`
- **Schema SQL:** See `schema/tools/schema.sql`

## License

BSD 3-Clause License. See [LICENSE](../LICENSE) file.

---

**Trust these instructions.** Only perform additional exploration if information is missing or incorrect.
