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

# Increase the adaptive sampler point budget (default: 50)
maliput-malidrive-to-gpkg map.xodr --num-samples 150

# Tighten the maximum polyline deviation used for adaptive refinement
maliput-malidrive-to-gpkg map.xodr --max-chord-error 0.01

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
| `--num-samples` | Maximum sample points per lane boundary | `50` |
| `--max-chord-error` | Maximum boundary polyline deviation in meters before adaptive refinement | `0.05` |
| `--linear-tolerance` | Linear tolerance in meters | `5e-2` |
| `--angular-tolerance` | Angular tolerance in radians | `1e-3` |
| `--omit-nondrivable-lanes` | Skip non-drivable lanes | `false` |
| `--template` | Path to the template `.gpkg` file | `../../schema/tools/template.gpkg` |

## How it works

1. Loads the `.xodr` file via `maliput.plugin.create_road_network("maliput_malidrive", ...)`.
2. Iterates over every junction → segment → lane in the road geometry.
3. Samples left and right lane boundaries adaptively. The sampler probes `lane.GetCurvature(...)` and midpoint geometry, keeping straight sections coarse while refining curved sections until the maximum polyline deviation is below `--max-chord-error` or `--num-samples` is reached.
4. Detects shared boundaries between laterally-adjacent lanes so each physical boundary is stored only once.
5. Extracts branch-point connectivity from the road geometry.
6. Extracts lane markings from OpenDRIVE `roadMark` definitions and maps them to exported lane boundaries.
7. Writes everything into a GeoPackage file (copied from `schema/tools/template.gpkg`) with the maliput_geopackage schema tables: `junctions`, `segments`, `lane_boundaries`, `lanes`, `branch_point_lanes`, `speed_limits`, `lane_markings`, and `lane_marking_lines`.

## Notes

- The GeoPackage template (`template.gpkg`) defaults to `../../schema/tools/template.gpkg` relative to this script. A custom template can be provided via the `--template` flag.
- Lane-marking extraction currently maps non-center lane `roadMark` data to the lane's outer boundary, and center-lane (`id=0`) `roadMark` data to the boundary between lane `+1` and lane `-1` when present.
- `--num-samples` now acts as an upper bound for the adaptive sampler rather than a fixed number of evenly-spaced points.
