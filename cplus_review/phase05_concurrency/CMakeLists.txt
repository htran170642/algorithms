# Concurrency (W32-W40)
cr_add_test(phase05_concurrency_test SOURCES
  tests/placeholder_test.cpp
  tests/counter_test.cpp          # W32 — data race vs mutex-guarded counter, jthread, TSan
)
find_package(Threads REQUIRED)
target_link_libraries(phase05_concurrency_test PRIVATE Threads::Threads)
target_include_directories(phase05_concurrency_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
