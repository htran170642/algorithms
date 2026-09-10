add_library(av_service
    src/someip.cpp
    src/sd.cpp
)

target_include_directories(av_service PUBLIC include)
target_link_libraries(av_service
    # Only av_log is PUBLIC-worthy here: someip.hpp holds no type from another
    # library, so a consumer needs nothing but this header. av_eth is
    # deliberately absent -- the wire format does not know it will travel over
    # UDP, and week 9's Service Discovery is what will join the two.
    PUBLIC av_log
    PRIVATE av_warnings
)

# Pure functions over byte buffers: no socket, no vcan0, nothing to skip.
av_add_test(av_service_someip_test
    SOURCES tests/someip_test.cpp
    LIBS av_service)

# Service Discovery, including its TTL state machine. The clock is a parameter
# rather than a call to now(), so expiry is tested in microseconds instead of
# by sleeping through a lease.
av_add_test(av_service_sd_test
    SOURCES tests/sd_test.cpp
    LIBS av_service)
