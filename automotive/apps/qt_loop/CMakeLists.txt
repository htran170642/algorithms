add_executable(qt_loop
    main.cpp
    needle.cpp
    window.cpp
    worker.cpp
    # The headers are listed because they carry Q_OBJECT, and AUTOMOC only looks
    # at headers the target names. See libs/av_qt/CMakeLists.txt for the link
    # error that follows from forgetting one.
    needle.hpp
    window.hpp
    worker.hpp
)

set_target_properties(qt_loop PROPERTIES AUTOMOC ON)

target_link_libraries(qt_loop PRIVATE
    av_qt          # LoopMonitor -- the instrument this demo is built around
    Qt6::Widgets
    av_warnings
)
