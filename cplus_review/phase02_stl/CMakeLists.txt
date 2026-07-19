# STL Deep Dive (W11-W20)
cr_add_test(phase02_stl_test SOURCES
  tests/iterator_test.cpp       # W11 — iterators, <algorithm>, ranges
  tests/containers_test.cpp     # W12 — vector/array/deque, iterator invalidation
)
target_include_directories(phase02_stl_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
