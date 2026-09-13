# Qt, found optionally.
#
# Week 11 is the first dependency that is not on the distro's default search
# path: the Qt installer puts it under ~/Qt/<version>/gcc_64. A machine without
# Qt must still build weeks 1-10, so nothing here is REQUIRED. AV_HAVE_QT gates
# the week 11+ targets and the rest of the tree never notices.
#
#   cmake --preset debug                                    # uses the hint below
#   cmake --preset debug -DCMAKE_PREFIX_PATH=/opt/Qt/6.9.0  # or say it yourself
#
# Must be included after Sanitizers, StaticAnalysis and Testing: it reads
# AV_SANITIZER and AV_CLANG_TIDY, and it wraps av_add_test().

# A hint, not a decision: an explicit CMAKE_PREFIX_PATH or Qt6_DIR still wins.
file(GLOB _av_qt_hints "$ENV{HOME}/Qt/6.*/gcc_64")
list(SORT _av_qt_hints)
list(REVERSE _av_qt_hints)  # newest version first

find_package(Qt6 6.2 QUIET COMPONENTS Core Gui Widgets Test HINTS ${_av_qt_hints})

set(AV_HAVE_QT ${Qt6_FOUND})

# TSan and a prebuilt Qt do not mix. ThreadSanitizer only reasons correctly
# about code it instrumented; the installed Qt is not, so every handoff inside
# QThread and the event dispatcher is reported as a race in *our* stack trace.
# Suppressing them one by one would hide the real ones, so week 11's targets are
# simply absent from the tsan preset -- and said out loud rather than silently.
# Their thread story is instead proved under ASan and by an explicit
# QThread::wait() before teardown.
if(AV_HAVE_QT AND AV_SANITIZER MATCHES "thread")
    set(AV_HAVE_QT OFF)
    message(STATUS "Qt: found but skipped under TSan (uninstrumented library)")
endif()

if(AV_HAVE_QT)
    message(STATUS "Qt: ${Qt6_VERSION} at ${Qt6_DIR}")

    # moc writes C++ into the build tree, and clang-tidy would happily review
    # it. Nobody can act on those warnings. clang-tidy resolves its config by
    # walking up from each file, so one file at the top of the build directory
    # excuses every generated source -- and nothing else, because generated
    # sources are all that lives there.
    if(AV_CLANG_TIDY)
        file(WRITE "${CMAKE_BINARY_DIR}/.clang-tidy"
             "# Generated sources (moc). Not ours to lint.\nChecks: '-*'\n")
    endif()
else()
    message(STATUS "Qt: not found -- week 11+ targets skipped, weeks 1-10 unaffected")
endif()

# av_add_qt_test(<name> SOURCES <files...> [LIBS <targets...>])
#
# av_add_test plus the two things a Qt test needs: moc, and a platform plugin
# that does not want a screen.
function(av_add_qt_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBS")

    av_add_test(${name} SOURCES ${ARG_SOURCES} LIBS ${ARG_LIBS})

    # moc, for any Q_OBJECT in the sources. Note that a header carrying
    # Q_OBJECT must itself appear in SOURCES: AUTOMOC does not go looking
    # through #includes for one.
    set_target_properties(${name} PROPERTIES AUTOMOC ON)

    # These tests exercise the event loop, not pixels. offscreen needs no X
    # server, which is also what lets them run anywhere.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
