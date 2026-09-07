add_executable(eth_gw main.cpp)
target_link_libraries(eth_gw PRIVATE av_can av_eth av_log av_warnings)

target_compile_definitions(eth_gw
    PRIVATE AV_DBC_PATH="${CMAKE_SOURCE_DIR}/dbc/cockpit.dbc")
