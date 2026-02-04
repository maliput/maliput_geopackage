ament_add_gtest(builder_configuration_test
  builder_configuration_test.cc
  ${PROJECT_SOURCE_DIR}/src/maliput_geopackage/builder/builder_configuration.cc
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
