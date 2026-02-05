// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "maliput_geopackage/builder/road_network_builder.h"

#include <memory>

#include <maliput/common/logger.h>
#include <maliput_sparse/loader/road_network_loader.h>

#include "maliput_geopackage/builder/builder_configuration.h"
#include "maliput_geopackage/geopackage/geopackage_parser.h"

namespace maliput_geopackage {
namespace builder {

std::unique_ptr<maliput::api::RoadNetwork> RoadNetworkBuilder::operator()() const {
  const BuilderConfiguration builder_config{BuilderConfiguration::FromMap(builder_config_)};

  maliput::log()->info("Loading GeoPackage from file: ", builder_config.gpkg_file, " ...");

  std::unique_ptr<maliput_sparse::parser::Parser> gpkg_parser =
      std::make_unique<geopackage::GeoPackageParser>(builder_config.gpkg_file);

  maliput::log()->trace("Building RoadNetwork...");
  return maliput_sparse::loader::RoadNetworkLoader(std::move(gpkg_parser), builder_config.sparse_config)();
}

}  // namespace builder
}  // namespace maliput_geopackage
