// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota
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
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <maliput/common/maliput_copyable.h>
#include <maliput_sparse/parser/connection.h>
#include <maliput_sparse/parser/junction.h>
#include <maliput_sparse/parser/parser.h>

namespace maliput_geopackage {
namespace geopackage {

/// GeoPackageParser is responsible for loading a GeoPackage file, parsing it according to the
/// maliput GeoPackage schema, and providing accessors to get the road network data.
///
/// TODO(#7): Implement this.
class GeoPackageParser : public maliput_sparse::parser::Parser {
 public:
  MALIPUT_NO_COPY_NO_MOVE_NO_ASSIGN(GeoPackageParser)

  /// Constructs a GeoPackageParser object.
  /// @param gpkg_file_path The path to the GeoPackage file to load.
  /// @throws std::runtime_error if the file cannot be opened or parsed.
  explicit GeoPackageParser(const std::string& gpkg_file_path);

  /// Destructor.
  ~GeoPackageParser();

 private:
  /// Gets the map's junctions.
  const std::unordered_map<maliput_sparse::parser::Junction::Id, maliput_sparse::parser::Junction>& DoGetJunctions()
      const override;

  /// Gets connections between the map's lanes.
  const std::vector<maliput_sparse::parser::Connection>& DoGetConnections() const override;
};

}  // namespace geopackage
}  // namespace maliput_geopackage
