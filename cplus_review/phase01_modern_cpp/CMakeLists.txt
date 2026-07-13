# Modern C++ (W1-W10)
cr_add_test(phase01_modern_cpp_test SOURCES
  tests/placeholder_test.cpp
)
target_include_directories(phase01_modern_cpp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
