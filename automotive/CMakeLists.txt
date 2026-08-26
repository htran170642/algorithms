cmake_minimum_required(VERSION 3.20)

project(automotive
    VERSION 0.1.0
    DESCRIPTION "Cockpit Domain Controller learning lab"
    LANGUAGES CXX)

# CLAUDE.md section 8 requires C++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed by clang-tidy and by editors/LSP.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Warnings)
include(Sanitizers)
include(StaticAnalysis)
include(Testing)

include(CTest)
find_package(GTest REQUIRED)

add_subdirectory(libs)
