# Warnings-as-errors is how the CLAUDE.md Coding Style stops being a preference.
# A style guide you can ignore is a style guide that decays over 14 months.

function(cr_enable_warnings target)
  target_compile_options(${target} INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow              # shadowed locals — a classic lifetime-bug source
    -Wconversion          # silent narrowing
    -Wsign-conversion
    -Wnon-virtual-dtor    # Phase 1 W7: deleting through a base without a virtual dtor
    -Woverloaded-virtual  # accidentally hiding instead of overriding
    -Wcast-align
    -Wdouble-promotion
    -Wnull-dereference
    -Werror
  )
endfunction()
