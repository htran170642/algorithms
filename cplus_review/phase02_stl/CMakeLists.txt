# STL Deep Dive (W11-W20)
cr_add_test(phase02_stl_test SOURCES
  tests/placeholder_test.cpp
)
target_include_directories(phase02_stl_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
