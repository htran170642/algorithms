add_executable(buffer_test
  buffer_pool_manager_test.cpp
  integration_test.cpp
)
target_link_libraries(buffer_test PRIVATE buffer_pool_manager GTest::gtest_main)
add_test(NAME BufferTest COMMAND buffer_test)
