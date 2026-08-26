# Shared warning set. Link any target against `av_warnings`.
#
# -Wconversion / -Wsign-conversion are deliberately on: week 1 is bit and
# endianness work, and silent integer narrowing is exactly the bug class that
# corrupts a decoded CAN signal.

add_library(av_warnings INTERFACE)

option(AV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

target_compile_options(av_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wcast-align
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wdouble-promotion
    -Wnull-dereference
    -Wformat=2
)

if(AV_WARNINGS_AS_ERRORS)
    target_compile_options(av_warnings INTERFACE -Werror)
endif()
