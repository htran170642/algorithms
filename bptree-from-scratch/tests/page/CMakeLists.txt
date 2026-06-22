add_executable(page_test
  page_test.cpp
  leaf_page_test.cpp
  internal_page_test.cpp
)
target_link_libraries(page_test PRIVATE bptree_headers GTest::gtest_main)
add_test(NAME PageTest COMMAND page_test)
