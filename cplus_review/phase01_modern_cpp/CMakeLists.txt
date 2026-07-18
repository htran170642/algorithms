# Modern C++ (W1-W10)
cr_add_test(phase01_modern_cpp_test SOURCES
  tests/probe_test.cpp          # W1 — value categories
  tests/noexcept_test.cpp       # W1 — why noexcept on move is not decoration
  tests/init_test.cpp           # W2 — constructors & initialization
  tests/raii_test.cpp           # W3 — RAII, Rule of 0/3/5, exception safety
  tests/string_test.cpp         # W4 — move semantics, SSO, copy-and-swap
  tests/vector_test.cpp         # W5 — placement new, growth, move_if_noexcept
  tests/forwarding_test.cpp     # W6 — perfect forwarding, reference collapsing
  tests/virtual_test.cpp        # W7 — virtual functions, slicing, NVI
  tests/object_model_test.cpp   # W8 — multiple/virtual inheritance, EBO
  tests/lifetime_test.cpp       # W9 — object lifetime, dangling, [[nodiscard]]
)
target_include_directories(phase01_modern_cpp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
