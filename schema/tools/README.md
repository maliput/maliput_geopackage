# GeoPackage Schema Tools

Language-agnostic tools for working with maliput GeoPackage databases.

## Philosophy

**Single Source of Truth**: The schema is defined once in `schema.sql`, and language-specific code loads it. This means:

- Schema changes are made in one place
- All languages use the identical schema
- No code generation, no duplication
- Clean and maintainable

## Files

- **`schema.sql`**: Complete SQLite schema definition (the single source of truth)
- **`example_simple_road.py`**: Example using Python

## Quick Start

### Python

Use standard `sqlite3` module and load the schema:

```python
import sqlite3
from pathlib import Path

# Load schema
db = sqlite3.connect('my_map.gpkg')
schema_path = Path(__file__).parent / 'schema.sql'
with open(schema_path) as f:
    db.executescript(f.read())

# Insert data
db.execute("INSERT INTO junctions (junction_id, name) VALUES (?, ?)",
           ('j1', 'Main Junction'))

db.execute("INSERT INTO lanes (lane_id, segment_id, left_boundary_id, right_boundary_id) VALUES (?, ?, ?, ?)",
           ('lane1', 'seg1', 'b_left', 'b_right'))

# Query data
cursor = db.execute("SELECT lane_id, lane_type FROM lanes")
for row in cursor:
    print(f"Lane {row[0]}: {row[1]}")

db.commit()
db.close()
```

Run the example:

```bash
python3 example_simple_road.py
```

### Rust

Use `rusqlite` crate:

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

    // Load schema
    let schema = fs::read_to_string("schema.sql")
        .expect("Failed to read schema.sql");
    db.execute_batch(&schema)?;

    // Insert data
    db.execute(
        "INSERT INTO junctions (junction_id, name) VALUES (?1, ?2)",
        ["j1", "Main Junction"],
    )?;

    db.execute(
        "INSERT INTO lanes (lane_id, segment_id, left_boundary_id, right_boundary_id) VALUES (?1, ?2, ?3, ?4)",
        ["lane1", "seg1", "b_left", "b_right"],
    )?;

    // Query data
    let mut stmt = db.prepare("SELECT lane_id, lane_type FROM lanes")?;
    let lanes = stmt.query_map([], |row| {
        Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
    })?;

    for lane in lanes {
        let (id, lane_type) = lane?;
        println!("Lane {}: {}", id, lane_type);
    }

    Ok(())
}
```

### C++

Use SQLite3 C API:

```cpp
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>

int main() {
    sqlite3* db;
    int rc = sqlite3_open("my_map.gpkg", &db);

    if (rc) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    // Load schema
    std::ifstream schema_file("schema.sql");
    std::stringstream schema_buffer;
    schema_buffer << schema_file.rdbuf();
    std::string schema = schema_buffer.str();

    char* err_msg = nullptr;
    rc = sqlite3_exec(db, schema.c_str(), nullptr, nullptr, &err_msg);

    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    // Insert data
    const char* insert_sql = "INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main Junction')";
    rc = sqlite3_exec(db, insert_sql, nullptr, nullptr, &err_msg);

    if (rc != SQLITE_OK) {
        std::cerr << "Insert error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }

    // Query data
    sqlite3_stmt* stmt;
    const char* query = "SELECT lane_id, lane_type FROM lanes";
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* lane_id = (const char*)sqlite3_column_text(stmt, 0);
        const char* lane_type = (const char*)sqlite3_column_text(stmt, 1);
        std::cout << "Lane " << lane_id << ": " << lane_type << std::endl;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}
```
