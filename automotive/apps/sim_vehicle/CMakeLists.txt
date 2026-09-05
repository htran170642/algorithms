add_executable(sim_vehicle main.cpp)
target_link_libraries(sim_vehicle PRIVATE av_can av_log av_warnings)

# Default database. Overridable at runtime with argv[3]; an absolute path from
# CMake means the demo runs from any working directory.
target_compile_definitions(sim_vehicle
    PRIVATE AV_DBC_PATH="${CMAKE_SOURCE_DIR}/dbc/cockpit.dbc")
