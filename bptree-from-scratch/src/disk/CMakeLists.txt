add_library(disk_manager STATIC disk_manager.cpp)
target_link_libraries(disk_manager PUBLIC bptree_headers)
