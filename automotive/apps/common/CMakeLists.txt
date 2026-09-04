# Header-only signal catalogue shared by the week-5 demos.
#
# INTERFACE because there is nothing to compile: it exists so the transmitter
# and the receiver read the same SignalSpec rows. Week 6 replaces it with a
# parsed .dbc file and this target goes away.

add_library(av_demo_signals INTERFACE)
target_include_directories(av_demo_signals INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av_demo_signals INTERFACE av_can)
