add_library(av_log
    src/log.cpp
)

target_include_directories(av_log PUBLIC include)
target_link_libraries(av_log PRIVATE av_warnings)

find_package(Threads REQUIRED)
target_link_libraries(av_log PUBLIC Threads::Threads)

av_add_test(av_log_test
    SOURCES tests/log_test.cpp
    LIBS av_log
)
