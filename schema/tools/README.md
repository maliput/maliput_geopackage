# GeoPackage Schema Tools

Language-agnostic tools and schema definitions for working with maliput GeoPackage databases, fully compliant with the OGC GeoPackage 1.3 specification.

This repository defines a canonical maliput GeoPackage schema and provides examples for creating and populating GeoPackage files from Python, Rust, and C++.

## Philosophy

**Single Source of Truth:** The maliput GeoPackage schema is defined once in schema.sql. From that definition we derive:

- A canonical template GeoPackage (.gpkg)
- Language-specific examples for populating data
- Clear, documented semantics for geometry and attributes

This ensures:

- Schema changes happen in one place
- All languages operate on identical database structures
- No schema duplication or code generation
- Long-term maintainability
- Interoperability with GDAL, QGIS, and other GeoPackage-aware tools

## GeoPackage Compliance

The schema is fully GeoPackage-compliant, specifically, schema.sql defines:

- GeoPackage identification
  - PRAGMA application_id = 'GPKG'
  - Application-defined user_version for maliput schema versioning
- Required core tables: gpkg_spatial_ref_sys, gpkg_contents, gpkg_geometry_columns, gpkg_extensions
- Required SRS entries
  - Undefined Cartesian (srs_id = -1)
  - Undefined Geographic (srs_id = 0)
- Properly registered feature tables
  - Geometry columns declared in gpkg_geometry_columns
  - Feature tables registered in gpkg_contents
- Standard GeoPackage geometry encoding
  - Geometry stored as BLOB
  - Encoded using GeoPackage Geometry Binary (WKB + header)

## Files

- **schema.sql** Complete, executable GeoPackage schema definition (core tables, maliput tables, feature registration, metadata)
- **template.gpkg** A pre-generated, empty GeoPackage created from schema.sql (see “Generating a GeoPackage file” below)

## Generating the template GeoPackage File

1. Execute and save `schema.sql`: `sqlite3 template.gpkg < schema.sql`
2. Distribute template.gpkg. Users copy the template and insert data

## Quick Start

### Python

Use standard `sqlite3` module and load the schema:

```python
import sqlite3
from pathlib import Path

db = sqlite3.connect("my_map.gpkg")

# Load schema (only if not using a template)
schema_path = Path(__file__).parent / "schema.sql"
with open(schema_path) as f:
    db.executescript(f.read())

# Insert schema-level metadata
db.execute(
    "INSERT INTO maliput_metadata (key, value) VALUES (?, ?)",
    ("linear_tolerance", "0.01"),
)

# Domain data
db.execute(
    "INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
    ("j1", "Main Junction"),
)
db.execute(
    "INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)",
    ("seg1", "j1", "Main Segment"),
)

# Feature table (geometry placeholder)
db.execute(
    "INSERT INTO boundaries (boundary_id, geometry) VALUES (?, ?)",
    ("b_left", b""),
)

db.execute(
    """
    INSERT INTO lanes
      (lane_id, segment_id, left_boundary_id, right_boundary_id)
    VALUES (?, ?, ?, ?)
    """,
    ("lane1", "seg1", "b_left", "b_right"),
)

db.commit()
db.close()
```

### Rust

Use `rusqlite` crate (with the `bundled` feature for SQLite):

```toml
[dependencies]
rusqlite = { version = "0.29", features = ["bundled"] }
```

Load schema and use standard rusqlite API:

```rust
use rusqlite::Connection;
use std::fs;

fn main() -> rusqlite::Result<()> {
    let db = Connection::open("my_map.gpkg")?;

    // Load schema (skip if using a template GeoPackage)
    let schema = fs::read_to_string("schema.sql").unwrap();
    db.execute_batch(&schema)?;

    db.execute(
        "INSERT INTO maliput_metadata (key, value) VALUES (?1, ?2)",
        ("linear_tolerance", "0.01"),
    )?;

    db.execute(
        "INSERT INTO junctions (junction_id, name) VALUES (?1, ?2)",
        ("j1", "Main Junction"),
    )?;

    db.execute(
        "INSERT INTO boundaries (boundary_id, geometry) VALUES (?1, ?2)",
        ("b_left", &[] as &[u8]),
    )?;

    Ok(())
}
```

### C++

Use SQLite3 C API:

```cpp
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <iostream>

int main() {
    sqlite3* db;
    if (sqlite3_open("my_map.gpkg", &db) != SQLITE_OK) {
        std::cerr << "Failed to open database\n";
        return 1;
    }

    // Load schema (skip if using template)
    std::ifstream file("schema.sql");
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string schema = buffer.str();

    char* err = nullptr;
    if (sqlite3_exec(db, schema.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Schema error: " << err << "\n";
        sqlite3_free(err);
    }

    sqlite3_exec(
        db,
        "INSERT INTO maliput_metadata (key, value) "
        "VALUES ('linear_tolerance', '0.01')",
        nullptr, nullptr, nullptr
    );

    sqlite3_close(db);
    return 0;
}
```

### Notes on Geometry Encoding

Geometry columns must contain GeoPackage Geometry Binary. Do not store raw WKB or JSON, recommended encoders are:

- GDAL / OGR (C++, Python, Rust bindings)
- GEOS / Shapely (geometry) + GDAL (I/O)
