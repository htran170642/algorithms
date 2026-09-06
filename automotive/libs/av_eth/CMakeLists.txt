add_library(av_eth
    src/udp_socket.cpp
)

target_include_directories(av_eth PUBLIC include)
target_link_libraries(av_eth
    # PUBLIC for the same reason av_can is: udp_socket.hpp holds a UniqueFd
    # member, so every consumer needs the av_ipc headers. A second Rule-of-5
    # fd wrapper would be a second thing to get wrong.
    PUBLIC av_ipc av_log
    PRIVATE av_warnings
)

# Multicast cases skip themselves when the host has no multicast-capable
# interface, so a container or a locked-down machine still gets a green run.
# The pure-function cases (is_multicast, range checks) always execute.
av_add_test(av_eth_udp_socket_test
    SOURCES tests/udp_socket_test.cpp
    LIBS av_eth)
