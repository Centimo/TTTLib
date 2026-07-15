find_package(Boost REQUIRED)
find_package(fmt REQUIRED)

add_library(${PROJECT_NAME} STATIC
    core/SRSW_cell.hpp
    core/utils.hpp
    core/utils/general.hpp
    core/utils/general.cpp
    core/utils/meta.hpp
    core/utils/string.hpp
    core/utils/string.cpp
    core/utils/time.hpp
    core/utils/time.cpp
    core/Thread.hpp
    core/Thread.cpp
    core/MPSC_queue.hpp
    core/MRSW_cell.hpp
    core/MRSW_cell.cpp
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    Boost::headers
    fmt::fmt
)
