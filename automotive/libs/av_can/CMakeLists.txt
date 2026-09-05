add_library(av_can
    src/bit.cpp
    src/frame.cpp
    src/signal.cpp
    src/arbitration.cpp
    src/error_state.cpp
    src/socket.cpp
    src/dbc.cpp
)

target_include_directories(av_can PUBLIC include)
target_link_libraries(av_can
    # PUBLIC: socket.hpp holds a UniqueFd member, so every consumer needs the
    # av_ipc headers. Reusing it beats a second Rule-of-5 fd wrapper here.
    PUBLIC av_ipc av_log
    PRIVATE av_warnings
)

av_add_test(av_can_bit_test         SOURCES tests/bit_test.cpp         LIBS av_can)
av_add_test(av_can_frame_test       SOURCES tests/frame_test.cpp       LIBS av_can)
av_add_test(av_can_signal_test      SOURCES tests/signal_test.cpp      LIBS av_can)
av_add_test(av_can_arbitration_test SOURCES tests/arbitration_test.cpp LIBS av_can)
av_add_test(av_can_error_state_test SOURCES tests/error_state_test.cpp LIBS av_can)

# Needs a vcan0 interface. Every case that does skips itself when it is absent,
# so a machine that has not run scripts/setup_vcan.sh still gets a green run.
av_add_test(av_can_socket_test      SOURCES tests/socket_test.cpp      LIBS av_can)

# The DBC test reads the project's own database, so it needs its path. An
# absolute path from CMake beats a relative one: ctest's working directory is
# the build tree, not the source tree.
av_add_test(av_can_dbc_test         SOURCES tests/dbc_test.cpp         LIBS av_can)
target_compile_definitions(av_can_dbc_test
    PRIVATE AV_DBC_PATH="${CMAKE_SOURCE_DIR}/dbc/cockpit.dbc")
