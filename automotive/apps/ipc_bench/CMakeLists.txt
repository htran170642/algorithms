add_executable(ipc_bench main.cpp)
target_link_libraries(ipc_bench PRIVATE av_ipc av_log av_warnings)
