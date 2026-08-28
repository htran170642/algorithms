add_executable(cockpit main.cpp)
target_link_libraries(cockpit PRIVATE av_can av_conc av_log av_warnings)
