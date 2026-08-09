# Performance (W41-W44)
cr_add_test(phase06_performance_test SOURCES
  tests/placeholder_test.cpp
)
target_include_directories(phase06_performance_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# W43 — copy elision / RVO / NRVO, proven with the W1 Probe counters.
# The SAME source is compiled twice: once normally, once with optional elision
# turned off. Guaranteed prvalue elision survives the flag; NRVO does not.
cr_add_test(phase06_elision_test SOURCES tests/elision_test.cpp)
target_include_directories(phase06_elision_test
  PRIVATE ${CMAKE_SOURCE_DIR}/phase01_modern_cpp/include)

cr_add_test(phase06_elision_noelide_test SOURCES tests/elision_test.cpp)
target_include_directories(phase06_elision_noelide_test
  PRIVATE ${CMAKE_SOURCE_DIR}/phase01_modern_cpp/include)
target_compile_definitions(phase06_elision_noelide_test PRIVATE CR_NO_ELIDE)
target_compile_options(phase06_elision_noelide_test PRIVATE -fno-elide-constructors)
