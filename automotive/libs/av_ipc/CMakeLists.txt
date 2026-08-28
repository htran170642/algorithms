add_library(av_ipc
    src/unix_socket.cpp
    src/poller.cpp
    src/shared_ring.cpp
)

target_include_directories(av_ipc PUBLIC include)
target_link_libraries(av_ipc
    PUBLIC av_log        # error paths log; callers get the header transitively
    PRIVATE av_warnings
)

av_add_test(av_ipc_socket_poller_test
    SOURCES tests/socket_poller_test.cpp
    LIBS av_ipc)

av_add_test(av_ipc_shared_ring_test
    SOURCES tests/shared_ring_test.cpp
    LIBS av_ipc)
