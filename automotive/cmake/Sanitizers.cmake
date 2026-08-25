# Sanitizers are the substrate of this lab, not an afterthought. You cannot learn
# object lifetime without ASAN telling you when you got it wrong, or memory_order
# without TSAN. They are wired in at Week 0 for that reason.
#
# Selected by preset via -DAV_SANITIZER=<none|address|undefined|thread>.
# ASan and TSan are mutually exclusive; "address" here means address+undefined,
# which is the pairing you want for Phases 1-2.

set(AV_SANITIZER "none" CACHE STRING "none | address | undefined | thread")
set_property(CACHE AV_SANITIZER PROPERTY STRINGS none address undefined thread)

if(AV_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)

elseif(AV_SANITIZER STREQUAL "undefined")
  # Standalone UBSan: no -fsanitize-trap, so it prints and keeps going and a single
  # run surfaces every UB site rather than dying at the first one.
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=undefined)

elseif(AV_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)

elseif(NOT AV_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "AV_SANITIZER must be none|address|undefined|thread, got '${AV_SANITIZER}'")
endif()

if(NOT AV_SANITIZER STREQUAL "none")
  message(STATUS "Sanitizer: ${AV_SANITIZER}")
endif()
