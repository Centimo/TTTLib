find_package(GTest REQUIRED)

add_executable(tttlib_tests
    meta_tests.cpp
    string_tests.cpp
    concepts_tests.cpp
    enum_array_tests.cpp
    enum_tuple_tests.cpp
    general_tests.cpp
    ranges_tests.cpp
)

# TTTLibrary carries the include path (src/lib) and fmt as PUBLIC usage requirements.
target_link_libraries(tttlib_tests PRIVATE TTTLibrary GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tttlib_tests)
