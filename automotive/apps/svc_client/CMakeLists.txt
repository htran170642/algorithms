add_executable(svc_client main.cpp)
target_link_libraries(svc_client PRIVATE av_service av_eth av_ipc av_log av_warnings)
