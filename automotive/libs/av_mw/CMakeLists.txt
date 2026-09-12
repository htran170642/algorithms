add_library(av_mw
    src/result.cpp
    src/runtime.cpp
    src/proxy.cpp
    src/skeleton.cpp
    src/vehicle.cpp
)

target_include_directories(av_mw PUBLIC include)
target_link_libraries(av_mw
    # PUBLIC: the headers name service ids, return codes and SD options, so a
    # consumer needs av_service's headers as well.
    PUBLIC av_service
    # PRIVATE, and this is week 10's boundary made mechanical. No av_mw header
    # names a socket or a poller -- the Runtime's pimpl and the Proxy and
    # Skeleton State structs keep them in the .cpp files -- so an application
    # that links av_mw cannot reach av_eth or av_ipc even by accident. The
    # compiler enforces the API boundary instead of a code review.
    PRIVATE av_eth av_ipc av_log av_warnings
)

# Integration tests, not unit tests: two runtimes, real UDP sockets on
# loopback, driven from one thread. They use their own SD group and ports so a
# demo left running in another terminal cannot answer on their behalf.
av_add_test(av_mw_test
    SOURCES tests/mw_test.cpp
    LIBS av_mw)
