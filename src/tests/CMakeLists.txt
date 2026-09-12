find_package(GTest REQUIRED)

add_executable(tttlib_tests
    at_least_tests.cpp
    meta_tests.cpp
    string_tests.cpp
    concepts_tests.cpp
    enums/Array_tests.cpp
    enums/Tuple_tests.cpp
    enums/Variant_tests.cpp
    enums/With_names_tests.cpp
    general_tests.cpp
    ranges_tests.cpp
    time_tests.cpp
)

# TTTLibrary carries the include path (src/lib) and fmt as PUBLIC usage requirements.
target_link_libraries(tttlib_tests PRIVATE TTTLibrary GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tttlib_tests)
