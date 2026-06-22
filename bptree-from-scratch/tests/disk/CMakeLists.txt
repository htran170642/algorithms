add_executable(disk_test disk_manager_test.cpp)
target_link_libraries(disk_test PRIVATE disk_manager GTest::gtest_main)
add_test(NAME DiskTest COMMAND disk_test)
