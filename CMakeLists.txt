cmake_minimum_required(VERSION 3.20)
project(WarpForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WARPFORGE_WERROR "Treat warnings as errors" OFF)
option(WARPFORGE_BUILD_TESTS "Build the test suite" ON)

include(cmake/Warnings.cmake)

# ---------------------------------------------------------------------------
# Core compiler library: everything except the two executables' main().
# ---------------------------------------------------------------------------
add_library(warpforge_core STATIC
  src/frontend/source.cpp
  src/frontend/diagnostics.cpp
)
target_include_directories(warpforge_core PUBLIC src)
target_compile_definitions(warpforge_core PUBLIC WARPFORGE_VERSION="${PROJECT_VERSION}")
warpforge_set_warnings(warpforge_core)

add_executable(warpforge tools/warpforge/main.cpp)
target_link_libraries(warpforge PRIVATE warpforge_core)
warpforge_set_warnings(warpforge)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WARPFORGE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
