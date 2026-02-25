# maliput_malidrive_to_gpkg

Converts a [maliput](https://github.com/maliput/maliput) road network backed by [maliput_malidrive](https://github.com/maliput/maliput_malidrive) (OpenDRIVE) into a [GeoPackage](https://www.geopackage.org/) (`.gpkg`) file conforming to the `maliput_geopackage` schema.

The script uses the **maliput Python API** to load the road network, sample every lane boundary from the geometry, and write the result into a GeoPackage database. This keeps the conversion simple and self-contained — no C++ compilation required.

## Setup

[uv](https://docs.astral.sh/uv/) is used for managing the Python environment and dependencies.

```bash
cd resources/maliput_malidrive_to_gpkg
uv sync
```

This creates a Python virtual environment and installs `maliput` and `maliput_malidrive` from the declared dependencies in `pyproject.toml`.

## Usage

The tool can be invoked either as an entry point or as a script:

```bash
# Using the entry point (installed by uv sync)
maliput-malidrive-to-gpkg /path/to/TShapeRoad.xodr

# Or as a script
uv run python maliput_malidrive_to_gpkg.py /path/to/TShapeRoad.xodr

# Specify output path
maliput-malidrive-to-gpkg /path/to/Town01.xodr -o town01.gpkg

# Increase boundary sample resolution (default: 50)
maliput-malidrive-to-gpkg map.xodr --num-samples 150

# Only export drivable lanes (omit sidewalks, shoulders, etc.)
maliput-malidrive-to-gpkg map.xodr --omit-nondrivable-lanes

# Use a custom template GeoPackage
maliput-malidrive-to-gpkg map.xodr --template /path/to/custom_template.gpkg
```

### CLI Reference

| Argument | Description | Default |
|---|---|---|
| `xodr_file` | Path to the input `.xodr` file | *required* |
| `-o`, `--output` | Output `.gpkg` file path | `<input_stem>.gpkg` |
| `--num-samples` | Sample points per lane boundary | `50` |
| `--linear-tolerance` | Linear tolerance in meters | `5e-2` |
| `--angular-tolerance` | Angular tolerance in radians | `1e-3` |
| `--omit-nondrivable-lanes` | Skip non-drivable lanes | `false` |
| `--template` | Path to the template `.gpkg` file | `../../schema/tools/template.gpkg` |

## How it works

1. Loads the `.xodr` file via `maliput.plugin.create_road_network("maliput_malidrive", ...)`.
2. Iterates over every junction → segment → lane in the road geometry.
3. Samples left and right lane boundaries at evenly-spaced `s` positions using `lane.lane_bounds(s)` and `lane.ToInertialPosition(...)`.
4. Detects shared boundaries between laterally-adjacent lanes so each physical boundary is stored only once.
5. Extracts branch-point connectivity from the road geometry.
6. Writes everything into a GeoPackage file (copied from `schema/tools/template.gpkg`) with the maliput_geopackage schema tables: `junctions`, `segments`, `lane_boundaries`, `lanes`, and `branch_point_lanes`.

## Notes

- The GeoPackage template (`template.gpkg`) defaults to `../../schema/tools/template.gpkg` relative to this script. A custom template can be provided via the `--template` flag.
- Lane type is currently set to `"driving"` for all exported lanes. A future improvement could infer the type from the OpenDRIVE lane type attribute.
