find_package(GTest REQUIRED)

# NOTE: the tests compile the sources under test directly instead of linking the
# TTTLibrary target, because that target does not build yet: SRSW_cell / Thread
# still depend on external cvs headers (items 10-11). Switch to linking the
# library once those are removed.
add_executable(tttlib_tests
    meta_tests.cpp
    string_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/lib/core/utils/general.cpp
    ${CMAKE_SOURCE_DIR}/src/lib/core/utils/string.cpp
)

target_include_directories(tttlib_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/lib)

target_link_libraries(tttlib_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tttlib_tests)
