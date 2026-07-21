# STL Deep Dive (W11-W20)
cr_add_test(phase02_stl_test SOURCES
  tests/iterator_test.cpp       # W11 — iterators, <algorithm>, ranges
  tests/containers_test.cpp     # W12 — vector/array/deque, iterator invalidation
  tests/hash_map_test.cpp       # W14 — open-addressing hash map
  tests/avl_tree_test.cpp       # W15 — self-balancing BST (AVL) -> map/set
)
target_include_directories(phase02_stl_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
