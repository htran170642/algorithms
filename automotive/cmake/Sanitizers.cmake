# Sanitizer selection, driven by the CMake presets.
#
#   AV_SANITIZER=none                 plain build
#   AV_SANITIZER=address,undefined    asan preset
#   AV_SANITIZER=undefined            ubsan preset
#   AV_SANITIZER=thread               tsan preset
#
# Applied globally (not as an INTERFACE target) because a sanitizer only gives
# reliable answers when every translation unit and the runtime agree.

set(AV_SANITIZER "none" CACHE STRING "Comma-separated sanitizers: none,address,undefined,thread")

if(NOT AV_SANITIZER STREQUAL "none")
    string(REPLACE "," ";" _av_sanitizers "${AV_SANITIZER}")

    # ASan and TSan use incompatible shadow-memory layouts.
    if("thread" IN_LIST _av_sanitizers AND "address" IN_LIST _av_sanitizers)
        message(FATAL_ERROR "AV_SANITIZER: 'thread' and 'address' cannot be combined")
    endif()

    set(_av_sanitizer_flags "")
    foreach(_san IN LISTS _av_sanitizers)
        list(APPEND _av_sanitizer_flags "-fsanitize=${_san}")
    endforeach()

    # Keep frame pointers so sanitizer stack traces are readable.
    add_compile_options(${_av_sanitizer_flags} -fno-omit-frame-pointer -g)
    add_link_options(${_av_sanitizer_flags})

    # Without this UBSan only prints and keeps going, so ctest still passes.
    if("undefined" IN_LIST _av_sanitizers)
        add_compile_options(-fno-sanitize-recover=undefined)
    endif()

    # TSan maps its shadow memory at fixed addresses. Kernel 6.x on Ubuntu 24.04
    # defaults to vm.mmap_rnd_bits=32, which randomises the binary into that
    # region and aborts with "unexpected memory mapping" before main() runs.
    # `setarch -R` disables ASLR for the child only -- no root, no sysctl edit.
    if("thread" IN_LIST _av_sanitizers)
        find_program(AV_SETARCH_EXE NAMES setarch)
        if(AV_SETARCH_EXE)
            set(AV_TEST_LAUNCHER "${AV_SETARCH_EXE}" "${CMAKE_SYSTEM_PROCESSOR}" "-R")
            message(STATUS "TSan: launching tests via setarch -R (ASLR disabled)")
        else()
            message(WARNING "TSan needs `setarch`; install util-linux or run "
                            "`sudo sysctl vm.mmap_rnd_bits=28`")
        endif()
    endif()

    message(STATUS "Sanitizers enabled: ${AV_SANITIZER}")
endif()
