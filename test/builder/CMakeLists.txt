ament_add_gtest(builder_configuration_test
  builder_configuration_test.cc
  ${PROJECT_SOURCE_DIR}/src/maliput_geopackage/builder/builder_configuration.cc
)

ament_add_gtest(road_network_builder_test
  road_network_builder_test.cc
)

target_compile_definitions(road_network_builder_test PRIVATE
  TEST_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/test/resources"
)

target_link_libraries(road_network_builder_test
  maliput_geopackage::builder
  maliput::api
  maliput::base
)

macro(add_dependencies_to_test target)
  if (TARGET ${target})
    target_include_directories(${target}
      PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(${target}
        maliput::api
        maliput::math
        maliput_sparse::loader
        maliput_sparse::parser
    )
  endif()
endmacro()

add_dependencies_to_test(builder_configuration_test)
