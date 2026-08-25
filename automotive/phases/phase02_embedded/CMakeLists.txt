# W3: volatile — proven from generated assembly, not asserted in prose.
#
# The probe is compiled STANDALONE at -O2, deliberately bypassing the project's
# flags. A sanitizer build instruments every memory access and a debug build
# optimises nothing, so either would destroy the very thing being measured. The
# question "does the compiler elide this read?" only has an answer at -O2.

set(AV_PROBE_ASM ${CMAKE_CURRENT_BINARY_DIR}/elision_probe.s)

add_custom_command(
  OUTPUT ${AV_PROBE_ASM}
  COMMAND ${CMAKE_CXX_COMPILER}
          -O2 -S -std=c++${CMAKE_CXX_STANDARD}
          ${CMAKE_CURRENT_SOURCE_DIR}/src/elision_probe.cpp
          -o ${AV_PROBE_ASM}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/elision_probe.cpp
  COMMENT "W3: compiling volatile elision probe to assembly at -O2"
  VERBATIM
)
add_custom_target(phase02_probe_asm DEPENDS ${AV_PROBE_ASM})

av_add_test(phase02_volatile_test SOURCES tests/volatile_test.cpp)
add_dependencies(phase02_volatile_test phase02_probe_asm)
target_compile_definitions(phase02_volatile_test PRIVATE AV_PROBE_ASM="${AV_PROBE_ASM}")
