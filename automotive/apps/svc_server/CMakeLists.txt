add_executable(svc_server main.cpp)

# No AV_DBC_PATH here, unlike can_rx and eth_gw. Week 8 is about the service
# boundary, not about decoding signals -- the server produces a float and the
# client consumes a float, and neither needs the DBC to agree on that. Week 10
# is where the two halves meet again.
target_link_libraries(svc_server PRIVATE av_service av_eth av_ipc av_log av_warnings)
