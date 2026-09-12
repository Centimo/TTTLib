find_package(GTest REQUIRED)

add_executable(tttlib_tests
    at_least.cpp
    meta.cpp
    string.cpp
    concepts.cpp
    enums/array.cpp
    enums/tuple.cpp
    enums/variant.cpp
    enums/with_names.cpp
    general.cpp
    ranges.cpp
    time.cpp
)

# TTTLibrary carries the include path (src/lib) and fmt as PUBLIC usage requirements.
target_link_libraries(tttlib_tests PRIVATE TTTLibrary GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tttlib_tests)
