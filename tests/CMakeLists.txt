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

# ---------------------------------------------------------------------------
# NVIDIA-toolchain validation (skipped with exit code 77 when unavailable)
# ---------------------------------------------------------------------------
find_program(WARPFORGE_PTXAS ptxas
  HINTS "$ENV{CUDA_PATH}/bin"
  PATHS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin" /usr/local/cuda/bin)
if(Python3_FOUND)
  set(ptxas_args "")
  if(WARPFORGE_PTXAS)
    set(ptxas_args --ptxas ${WARPFORGE_PTXAS})
  endif()
  add_test(NAME ptxas_golden
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_ptxas.py ${CMAKE_CURRENT_SOURCE_DIR}/golden
            ${ptxas_args})
  set_tests_properties(ptxas_golden PROPERTIES SKIP_RETURN_CODE 77 LABELS nvidia)
endif()

# Every example, optimized and unoptimized, runs on the GPU through the
# driver JIT and must match the CPU reference bit-for-bit.
file(GLOB WARPFORGE_EXAMPLES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/examples/*.wf)
foreach(example ${WARPFORGE_EXAMPLES})
  get_filename_component(kernel ${example} NAME_WE)
  foreach(variant ptx O0.ptx)
    add_test(NAME gpu_${kernel}_${variant}
      COMMAND wf-run --source ${example} --ptx ${CMAKE_CURRENT_SOURCE_DIR}/golden/${kernel}.${variant}
              --kernel ${kernel} --check --n 100000)
    set_tests_properties(gpu_${kernel}_${variant} PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu)
  endforeach()
endforeach()

# Hand-written IR in, expected IR out: each pass tested in isolation.
if(Python3_FOUND)
  add_test(NAME ir_passes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_ir_tests.py
            $<TARGET_FILE:warpforge> ${CMAKE_CURRENT_SOURCE_DIR}/ir)
endif()
