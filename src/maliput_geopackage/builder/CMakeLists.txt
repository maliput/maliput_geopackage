##############################################################################
# Builder
##############################################################################

add_library(builder
  builder_configuration.cc
  road_network_builder.cc
)

add_library(maliput_geopackage::builder ALIAS builder)

set_target_properties(builder
  PROPERTIES
    OUTPUT_NAME maliput_geopackage_builder
)

target_include_directories(builder
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(builder
  PUBLIC
    maliput::api
    maliput::common
    maliput_sparse::loader
  PRIVATE
    maliput_geopackage::geopackage
)

install(TARGETS builder
  EXPORT ${PROJECT_NAME}-targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
