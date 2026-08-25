# Warnings-as-errors is how the CLAUDE.md Coding Style stops being a preference.
# A style guide you can ignore is a style guide that decays over 8 months.
#
# Two tiers, because the strict tier fires inside GoogleTest macro expansions:
#
#   av_enable_warnings()        -> everything, tests included
#   av_enable_strict_warnings() -> libs/ only, the code that would ship in a vehicle
#
# The strict tier encodes rules MISRA C++ and AUTOSAR C++14 actually mandate, so
# W3's .clang-tidy gate is not the first time they are enforced.

function(av_enable_warnings target)
  target_compile_options(${target} INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow              # shadowed locals — a classic lifetime-bug source
    -Wconversion          # silent narrowing. Painful in CAN bit-twiddling; that is the point.
    -Wsign-conversion
    -Wnon-virtual-dtor    # deleting through a base without a virtual dtor
    -Woverloaded-virtual  # accidentally hiding instead of overriding
    -Wcast-align          # unaligned reinterpret_cast — a real fault on ARM, silent on x86
    -Wdouble-promotion    # float->double creep; costly on an MCU with a single-precision FPU
    -Wnull-dereference
    -Werror
  )
endfunction()

# Applied to libs/ targets only — tests opt out because GoogleTest's macros trip
# -Wold-style-cast, and suppressing it there would weaken it where it matters.
function(av_enable_strict_warnings target)
  target_compile_options(${target} PRIVATE
    -Wold-style-cast   # AUTOSAR C++14 A5-2-2 / MISRA: C-style casts banned outright
    -Wfloat-equal      # MISRA: never compare floats with ==
    -Wswitch-enum      # every enumerator handled explicitly — no silent gap when a DTC is added
    -Wundef            # an undefined macro in #if is a typo, not a zero
  )
endfunction()
