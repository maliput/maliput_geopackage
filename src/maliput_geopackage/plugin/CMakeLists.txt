##############################################################################
# Plugin
##############################################################################

add_library(road_network_plugin SHARED
  road_network.cc
)

add_library(maliput_geopackage::road_network_plugin ALIAS road_network_plugin)

set_target_properties(road_network_plugin
  PROPERTIES
    OUTPUT_NAME maliput_geopackage_road_network
)

target_include_directories(road_network_plugin
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(road_network_plugin
  PUBLIC
    maliput::plugin
    maliput_geopackage::builder
)

install(TARGETS road_network_plugin
  EXPORT ${PROJECT_NAME}-targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
