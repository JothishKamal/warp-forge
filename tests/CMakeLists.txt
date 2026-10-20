include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(warpforge_tests
  unit/test_diagnostics.cpp
)
target_link_libraries(warpforge_tests PRIVATE warpforge_core GTest::gtest_main)
warpforge_set_warnings(warpforge_tests)

include(GoogleTest)
gtest_discover_tests(warpforge_tests DISCOVERY_MODE PRE_TEST)
