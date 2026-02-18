##############################################################################
# GeoPackage Parser
##############################################################################

add_library(geopackage
  geopackage_parser.cc
)

add_library(maliput_geopackage::geopackage ALIAS geopackage)

set_target_properties(geopackage
  PROPERTIES
    OUTPUT_NAME maliput_geopackage_geopackage
)

target_include_directories(geopackage
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(geopackage
  PUBLIC
    maliput::common
    maliput::math
    maliput_sparse::geometry
    maliput_sparse::parser
    SQLite::SQLite3
)

install(TARGETS geopackage
  EXPORT ${PROJECT_NAME}-targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
