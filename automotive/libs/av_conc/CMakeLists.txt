# Header-only: BoundedQueue is a template, so there is nothing to compile into
# a .a. INTERFACE carries the include path and the Threads dependency to
# whoever links it.
add_library(av_conc INTERFACE)

target_include_directories(av_conc INTERFACE include)

find_package(Threads REQUIRED)
target_link_libraries(av_conc INTERFACE Threads::Threads)

av_add_test(av_conc_bounded_queue_test
    SOURCES tests/bounded_queue_test.cpp
    LIBS av_conc)
