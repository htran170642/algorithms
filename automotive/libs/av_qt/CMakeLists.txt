add_library(av_qt
    src/loop_monitor.cpp
    # Listed as a source although nothing compiles it, and this is load-bearing.
    # AUTOMOC only mocs a header that is either in the target's sources or sits
    # next to a same-named source; ours is in include/ while the source is in
    # src/, so without this line moc never sees the Q_OBJECT and the link fails
    # on `undefined reference to LoopMonitor::staticMetaObject`. That error is
    # the first one every Qt newcomer meets, and this is one of its two causes
    # (the other being a forgotten AUTOMOC).
    include/av/qt/loop_monitor.hpp
)

target_include_directories(av_qt PUBLIC include)

# moc turns Q_OBJECT into the metaobject table -- the thing that makes
# `connect(&timer, &QTimer::timeout, ...)` resolvable and a queued
# `emit stalled(...)` deliverable at all.
set_target_properties(av_qt PROPERTIES AUTOMOC ON)

target_link_libraries(av_qt
    # PUBLIC: the header inherits QObject and holds a QTimer by value, so
    # anything that includes it needs Qt6::Core's headers too.
    PUBLIC Qt6::Core
    PRIVATE av_warnings
)

# The split with apps/qt_loop is deliberate. av_qt holds the *instrument* and
# the *semantics*: a thing that measures loop latency, and tests that pin down
# when a slot runs, who disconnects it, and which thread it runs on. The three
# cures for a blocked loop are UI-driven, so they live in the demo where they
# can be clicked -- what is testable is tested here, and what is watchable is
# watched there.
av_add_qt_test(av_qt_test
    # signal_probe.hpp is listed for the same reason as the library header
    # above: moc only looks at headers the target names.
    SOURCES tests/qt_test.cpp tests/signal_probe.hpp
    LIBS av_qt Qt6::Test)
