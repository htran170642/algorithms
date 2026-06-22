add_executable(tree_test
  traversal_test.cpp
  insert_test.cpp
  delete_test.cpp
  iterator_test.cpp
  range_scan_test.cpp
)
target_link_libraries(tree_test PRIVATE bptree_headers GTest::gtest_main)
add_test(NAME TreeTest COMMAND tree_test)
