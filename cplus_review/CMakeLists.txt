cmake_minimum_required(VERSION 3.20)
project(cplus_review LANGUAGES CXX)

# Standard comes from the preset (-20 / -23 preset families), defaulting to C++23.
# Every target must compile and pass under BOTH.
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Warnings)
include(Sanitizers)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest benchmark)

enable_testing()

# Every phase links this: carries the include dirs, warnings, and language level.
add_library(cr_base INTERFACE)
target_include_directories(cr_base INTERFACE ${CMAKE_SOURCE_DIR})
target_compile_features(cr_base INTERFACE cxx_std_20)
cr_enable_warnings(cr_base)

# Feature detection: use the STANDARD feature-test macros in source
# (__cpp_lib_expected, __cpp_lib_ranges, ...) via <version>, NOT a CMake header
# check. check_include_file_cxx runs once at configure time with one standard and
# only tests that the HEADER exists — but <expected> ships in libstdc++ 13 even
# under -std=c++20, with std::expected guarded out inside it. That gave a false
# positive that broke the C++20 build. The feature-test macro respects the actual
# -std flag per translation unit, which is exactly what the dual-standard matrix
# needs. (Bug found by the matrix itself in W17.)

# TSan maps its shadow memory at FIXED addresses. Modern kernels (6.x, Ubuntu
# 24.04) randomize mmap with 32 bits of entropy, which intermittently lands a
# library inside TSan's reserved range:
#
#   FATAL: ThreadSanitizer: unexpected memory mapping 0x7946c5272000-...
#
# It's random, so it looks like a flaky test — it isn't. Run TSan binaries with
# ASLR off. setarch -R needs no root; the alternative is a system-wide
# `sudo sysctl -w vm.mmap_rnd_bits=28`, which we don't want to require.
set(CR_TEST_LAUNCHER "")
if(CR_SANITIZER STREQUAL "thread")
  find_program(SETARCH_EXE setarch REQUIRED)
  set(CR_TEST_LAUNCHER ${SETARCH_EXE} -R)
endif()

# --- helper: declare a phase's test binary with one line ---------------------
# cr_add_test(<name> SOURCES <files...>)
function(cr_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE cr_base GTest::gtest_main GTest::gmock)
  add_test(NAME ${name} COMMAND ${CR_TEST_LAUNCHER} $<TARGET_FILE:${name}>)
endfunction()

add_subdirectory(phase01_modern_cpp)
add_subdirectory(phase02_stl)
add_subdirectory(phase03_templates)
add_subdirectory(phase04_memory)
add_subdirectory(phase05_concurrency)
add_subdirectory(phase06_performance)
add_subdirectory(phase07_linux)
add_subdirectory(phase08_networking)
add_subdirectory(phase09_design)
add_subdirectory(challenges)
add_subdirectory(bench)
add_subdirectory(scratch)
