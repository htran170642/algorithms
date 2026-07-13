# Modern C++ (W1-W10)
cr_add_test(phase01_modern_cpp_test SOURCES
  tests/probe_test.cpp          # W1 — value categories
  tests/noexcept_test.cpp       # W1 — why noexcept on move is not decoration
  tests/init_test.cpp           # W2 — constructors & initialization
)
target_include_directories(phase01_modern_cpp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
