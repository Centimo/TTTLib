cmake_minimum_required(VERSION 3.25)
project(TTTLibrary CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TTTLIB_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/lib)

if(TTTLIB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(src/tests)
endif()
