add_executable(svc_client main.cpp)

# av_mw only -- see apps/svc_server/CMakeLists.txt. No socket is reachable
# from this target, which is the whole of week 10 in one line.
target_link_libraries(svc_client PRIVATE av_mw av_log av_warnings)
