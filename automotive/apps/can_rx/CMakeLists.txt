add_executable(can_rx main.cpp)
target_link_libraries(can_rx PRIVATE av_can av_ipc av_log av_demo_signals av_warnings)
