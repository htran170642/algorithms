add_library(av_can
    src/bit.cpp
    src/frame.cpp
    src/signal.cpp
)

target_include_directories(av_can PUBLIC include)
target_link_libraries(av_can PRIVATE av_warnings)

av_add_test(av_can_bit_test    SOURCES tests/bit_test.cpp    LIBS av_can)
av_add_test(av_can_frame_test  SOURCES tests/frame_test.cpp  LIBS av_can)
av_add_test(av_can_signal_test SOURCES tests/signal_test.cpp LIBS av_can)
