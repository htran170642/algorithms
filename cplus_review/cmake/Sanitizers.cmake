# Sanitizers are the substrate of this whole lab, not an afterthought — which is
# why Phase 10 got pulled to Week 0. You cannot learn object lifetime without ASAN
# telling you when you got it wrong, or memory_order without TSAN.
#
# Selected by preset via -DCR_SANITIZER=<none|address|undefined|thread>.
# ASan and TSan are mutually exclusive; "address" here means address+undefined,
# which is the pairing you want for Phases 1-4.

set(CR_SANITIZER "none" CACHE STRING "none | address | undefined | thread")
set_property(CACHE CR_SANITIZER PROPERTY STRINGS none address undefined thread)

if(CR_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)

elseif(CR_SANITIZER STREQUAL "undefined")
  # Standalone UBSan: trap-free, prints and keeps going, so a single run surfaces
  # every UB site rather than dying at the first one.
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=undefined)

elseif(CR_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)

elseif(NOT CR_SANITIZER STREQUAL "none")
  message(FATAL_ERROR "CR_SANITIZER must be none|address|undefined|thread, got '${CR_SANITIZER}'")
endif()

if(NOT CR_SANITIZER STREQUAL "none")
  message(STATUS "Sanitizer: ${CR_SANITIZER}")
endif()
