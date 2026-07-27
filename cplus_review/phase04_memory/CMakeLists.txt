# Memory Management (W27-W31)
cr_add_test(phase04_memory_test SOURCES
  tests/placeholder_test.cpp
  tests/layout_test.cpp           # W27 — alignment, padding, struct reordering, alignas
  tests/storage_test.cpp          # W28 — placement new, manual lifetime, std::launder
  tests/pool_test.cpp             # W30 — fixed-size object pool, free-list slot reuse
)
target_include_directories(phase04_memory_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
