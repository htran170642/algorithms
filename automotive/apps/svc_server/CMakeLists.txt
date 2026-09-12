add_executable(svc_server main.cpp)

# Week 10: av_mw, and nothing that opens a socket. This line used to read
# `av_service av_eth av_ipc` -- every layer the server drove by hand. av_mw
# links those PRIVATE, so this target cannot reach them even by accident: the
# link line is the API boundary, and the build checks it.
#
# Still no AV_DBC_PATH. The server produces a float and the client consumes
# one; week 14 is where the CAN side starts feeding it real signals.
target_link_libraries(svc_server PRIVATE av_mw av_log av_warnings)
