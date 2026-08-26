# Static analysis. Off by default so the normal edit/build loop stays fast;
# the `tidy` preset and check.sh turn it on.
#
# CLAUDE.md section 8 asks for static analysis "where practical", and section 15
# covers MISRA / coding guidelines. clang-tidy is the practical stand-in here.

option(AV_CLANG_TIDY "Run clang-tidy as part of the build" OFF)

if(AV_CLANG_TIDY)
    find_program(AV_CLANG_TIDY_EXE NAMES clang-tidy)
    if(AV_CLANG_TIDY_EXE)
        # --quiet suppresses the per-file "N warnings generated" noise.
        set(CMAKE_CXX_CLANG_TIDY "${AV_CLANG_TIDY_EXE};--quiet")
        message(STATUS "clang-tidy enabled: ${AV_CLANG_TIDY_EXE}")
    else()
        message(WARNING "AV_CLANG_TIDY=ON but clang-tidy was not found; skipping")
    endif()
endif()
