add_executable(sim_vehicle main.cpp)
target_link_libraries(sim_vehicle PRIVATE av_can av_log av_demo_signals av_warnings)
