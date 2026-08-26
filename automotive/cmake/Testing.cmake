# av_add_test(<name> SOURCES <files...> [LIBS <targets...>])
#
# Registers a GoogleTest executable with ctest. Every test gets the shared
# warning set, so test code is held to the same standard as library code.

function(av_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBS")

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "av_add_test(${name}): SOURCES is required")
    endif()

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE
        ${ARG_LIBS}
        av_warnings
        GTest::gtest
        GTest::gtest_main
    )

    # AV_TEST_LAUNCHER is empty except under TSan; see cmake/Sanitizers.cmake.
    add_test(NAME ${name} COMMAND ${AV_TEST_LAUNCHER} $<TARGET_FILE:${name}>)

    # Fail fast instead of hanging check.sh on a deadlocked test.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()
