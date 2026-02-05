# maliput_geopackage

## Description

`maliput_geopackage` is a [Maliput](https://github.com/maliput/maliput) backend implementation that loads road network data from [GeoPackage](https://www.geopackage.org/) files.

GeoPackage is an OGC standard format that uses SQLite as its container, providing:
- Spatial indexing (R-tree) for efficient queries
- Schema enforcement at database level
- Compatibility with GIS tools (QGIS, GDAL, etc.)
- Single-file distribution

This backend relies on [maliput_sparse](https://github.com/maliput/maliput_sparse) for building the road geometry from sampled lane boundaries.

## Documentation

- **[GeoPackage Schema](schema/README.md)**: Complete schema specification with examples.

## Supported platforms

Ubuntu Focal Fossa 20.04 LTS, ROS2 Foxy.

## Build

```bash
colcon build --packages-select maliput_geopackage
```

## Usage

### Basic Example

```cpp
#include <maliput_geopackage/builder/road_network_builder.h>

const std::map<std::string, std::string> builder_config {
  {"gpkg_file", "/path/to/road_network.gpkg"},
  {"road_geometry_id", "my_road_network"},
  {"linear_tolerance", "0.01"},
  {"angular_tolerance", "0.01"},
};

auto road_network = maliput_geopackage::builder::RoadNetworkBuilder(builder_config)();
```

## For development

It is recommended to follow the guidelines for setting up a development workspace as described [here](https://maliput.readthedocs.io/en/latest/developer_setup.html).

## Contributing

Please see [CONTRIBUTING](https://maliput.readthedocs.io/en/latest/contributing.html) page.

## License

[![License](https://img.shields.io/badge/License-BSD_3--Clause-blue.svg)](https://github.com/maliput/maliput_geopackage/blob/main/LICENSE)
