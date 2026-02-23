find_package(ament_cmake_gtest REQUIRED)

add_subdirectory(builder)

ament_add_gtest(sqlite_helpers_test
  sqlite_helpers_test.cc
)

target_compile_definitions(sqlite_helpers_test PRIVATE
  TEST_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(sqlite_helpers_test
  maliput_geopackage::geopackage
)

ament_add_gtest(geopackage_parser_test
  geopackage_parser_test.cc
)

target_compile_definitions(geopackage_parser_test PRIVATE
  TEST_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_link_libraries(geopackage_parser_test
  maliput_geopackage::geopackage
)
