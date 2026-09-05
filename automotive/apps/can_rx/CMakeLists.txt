add_executable(can_rx main.cpp)
target_link_libraries(can_rx PRIVATE av_can av_ipc av_log av_warnings)

target_compile_definitions(can_rx
    PRIVATE AV_DBC_PATH="${CMAKE_SOURCE_DIR}/dbc/cockpit.dbc")
