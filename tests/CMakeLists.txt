include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(GLOB WARPFORGE_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
add_executable(warpforge_tests ${WARPFORGE_TEST_SOURCES})
target_link_libraries(warpforge_tests PRIVATE warpforge_core GTest::gtest_main)
warpforge_set_warnings(warpforge_tests)

include(GoogleTest)
gtest_discover_tests(warpforge_tests DISCOVERY_MODE PRE_TEST)
target_compile_definitions(warpforge_tests PRIVATE WARPFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# CLI-level suites (need Python 3)
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME golden
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.py
            $<TARGET_FILE:warpforge> ${PROJECT_SOURCE_DIR}/examples ${CMAKE_CURRENT_SOURCE_DIR}/golden)
  add_test(NAME errors
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_errors.py
            $<TARGET_FILE:warpforge> ${CMAKE_CURRENT_SOURCE_DIR}/errors)
else()
  message(WARNING "Python 3 not found: golden and diagnostic suites are disabled")
endif()

# The PRD's illustrative commands must work verbatim.
add_test(NAME cli_emit_ptx
  COMMAND warpforge ${PROJECT_SOURCE_DIR}/examples/vector_add.wf --emit-ptx out/vector_add.ptx
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME cli_dump_ir_opt COMMAND warpforge ${PROJECT_SOURCE_DIR}/examples/vector_add.wf --dump-ir --opt)
add_test(NAME cli_dump_ir_no_opt COMMAND warpforge ${PROJECT_SOURCE_DIR}/examples/vector_add.wf --dump-ir --no-opt)
