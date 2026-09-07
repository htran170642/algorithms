add_executable(eth_sub main.cpp)
target_link_libraries(eth_sub PRIVATE av_can av_eth av_ipc av_log av_warnings)

target_compile_definitions(eth_sub
    PRIVATE AV_DBC_PATH="${CMAKE_SOURCE_DIR}/dbc/cockpit.dbc")
