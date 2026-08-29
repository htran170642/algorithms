add_executable(can_bus main.cpp)
target_link_libraries(can_bus PRIVATE av_can av_log av_warnings)
