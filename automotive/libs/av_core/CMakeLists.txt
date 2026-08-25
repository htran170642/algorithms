# av_core — the foundation every other lib and app links.
#
#   W1: fixed_vector.hpp, fixed_string.hpp        (no-heap containers)
#   W3: bit.hpp, endian.hpp, mmio.hpp, clock.hpp  (embedded C++)
#
# INTERFACE, not STATIC: there is no .cpp yet. A static library with no sources
# makes CMake complain and produces an empty archive; INTERFACE is the honest
# shape for header-only code.
#
# Note this means av_enable_strict_warnings() cannot apply yet — it is PRIVATE,
# and an INTERFACE library compiles nothing. Header-only code is therefore gated
# by clang-tidy (W3's .clang-tidy), not by the compiler. The compiler tier kicks
# in at W3 when mmio.cpp arrives and av_core becomes a real STATIC library.
add_library(av_core INTERFACE)
target_include_directories(av_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(av_core INTERFACE av_base)

av_add_test(av_core_fixed_vector_test
  SOURCES tests/fixed_vector_test.cpp
  LIBS    av_core)

av_add_test(av_core_fixed_string_test
  SOURCES tests/fixed_string_test.cpp
  LIBS    av_core)
