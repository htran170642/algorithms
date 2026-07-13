# Concurrency (W32-W40)
cr_add_test(phase05_concurrency_test SOURCES
  tests/placeholder_test.cpp
)
target_include_directories(phase05_concurrency_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
