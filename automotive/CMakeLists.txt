cmake_minimum_required(VERSION 3.25)
project(automotive_cdc LANGUAGES CXX)

# Standard comes from the preset (-17 / -20 preset families), defaulting to C++17.
#
# Why 17/20 and not 20/23: CLAUDE.md Tier A says "C++11/14/17", W3 studies the
# AUTOSAR C++14 guidelines, and MISRA C++:2023 is C++17-based. Shipping automotive
# code is C++14/17. Every artifact must compile and pass under BOTH so the
# feature-test fallbacks in av_core stay honest instead of decorative.
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Warnings)
include(Sanitizers)

# Declared explicitly rather than relying on GoogleTest's own find_package as a
# side effect. A dependency's internal calls are not part of its contract; when
# it reorganises, targets you never asked for simply vanish.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Benchmarks are opt-in. check.sh configures 8 presets on every run; fetching and
# building Google Benchmark in all of them costs more than it returns until W30.
option(AV_BUILD_BENCH "Fetch Google Benchmark and build benchmark targets" OFF)

include(FetchContent)

# SYSTEM keeps third-party headers from tripping our -Werror wall (needs CMake 3.25).
# GoogleTest >= 1.15 declares cmake_minimum_required 3.13, which CMake 4.x accepts;
# the 1.14.0 used elsewhere in this workspace does not configure cleanly under CMake 4.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  SYSTEM
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if(AV_BUILD_BENCH)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.0
    SYSTEM
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

enable_testing()

# Every target links this: carries the include root, the language floor, and warnings.
add_library(av_base INTERFACE)
target_include_directories(av_base INTERFACE ${CMAKE_SOURCE_DIR})
target_compile_features(av_base INTERFACE cxx_std_17)
av_enable_warnings(av_base)

# Feature detection: use the STANDARD feature-test macros in source
# (__cpp_lib_bitops, __cpp_lib_byteswap, ...) via <version>, NOT a CMake header
# check. check_include_file_cxx runs once at configure time with one standard and
# only tests that the HEADER exists — but a C++20 header can ship in libstdc++ 13
# with its contents guarded out under -std=c++17. The feature-test macro respects
# the actual -std flag per translation unit, which is what a dual-standard matrix
# needs.

# TSan maps its shadow memory at FIXED addresses. Modern kernels randomize mmap
# with enough entropy to intermittently land a library inside TSan's reserved range:
#
#   FATAL: ThreadSanitizer: unexpected memory mapping 0x7946c5272000-...
#
# It is random, so it looks like a flaky test — it isn't. Run TSan binaries with
# ASLR off. `setarch -R` needs no root; the alternative is a system-wide
# `sudo sysctl -w vm.mmap_rnd_bits=28`, which we don't want to require.
set(AV_TEST_LAUNCHER "")
if(AV_SANITIZER STREQUAL "thread")
  find_program(SETARCH_EXE setarch REQUIRED)
  set(AV_TEST_LAUNCHER ${SETARCH_EXE} -R)
endif()

# --- helper: declare a test binary with one line --------------------------------
# av_add_test(<name> SOURCES <files...> [LIBS <targets...>])
#
# Tests get av_enable_warnings but NOT av_enable_strict_warnings: GoogleTest's
# macros expand to C-style casts, and -Wold-style-cast cannot tell those from ours.
# The strict tier is applied to libs/ targets, where it guards shipping code.
function(av_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE av_base GTest::gtest_main GTest::gmock ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${AV_TEST_LAUNCHER} $<TARGET_FILE:${name}>)
endfunction()

add_subdirectory(libs)
add_subdirectory(phases)
