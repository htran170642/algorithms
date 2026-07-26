# Templates (W21-W26)
cr_add_test(phase03_templates_test SOURCES
  tests/placeholder_test.cpp
  tests/template_basics_test.cpp  # W21 — deduction, CTAD, specialization, lazy instantiation
  tests/variadic_test.cpp         # W22 — packs, expansion patterns, fold expressions
  tests/traits_test.cpp           # W23 — <type_traits> from scratch: value vs type traits
  tests/constraints_test.cpp      # W24 — SFINAE, enable_if, concepts: one constraint, four eras
  tests/crtp_test.cpp             # W25 — CRTP / static polymorphism vs virtual dispatch
)
target_include_directories(phase03_templates_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
