add_executable(common_test common_test.cpp)
target_link_libraries(common_test PRIVATE bptree_headers GTest::gtest_main)
add_test(NAME CommonTest COMMAND common_test)
