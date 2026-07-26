# Google Benchmark harness. Comes online properly in Phase 6 (W41), but exists
# from Week 0 so that any earlier week CAN measure instead of guess — the
# list-vs-vector week (W13) and the false-sharing week (W31) both need it.
#
# Always benchmark the RELEASE preset. Benchmarking a -O0 debug build measures
# nothing but the absence of the optimizer.

add_executable(bench_all bench_all.cpp)
target_link_libraries(bench_all PRIVATE cr_base benchmark::benchmark_main)

# W13 — does list really beat vector at mid-insertion? Measure, don't assume.
add_executable(bench_list_vs_vector bench_list_vs_vector.cpp)
target_link_libraries(bench_list_vs_vector PRIVATE cr_base benchmark::benchmark_main)

# W14 — open-addressing map vs std::unordered_map at lookup.
add_executable(bench_hashmap bench_hashmap.cpp)
target_link_libraries(bench_hashmap PRIVATE cr_base benchmark::benchmark_main)
target_include_directories(bench_hashmap PRIVATE ${CMAKE_SOURCE_DIR}/phase02_stl/include)

# W20 — pmr monotonic buffer vs the default allocator.
add_executable(bench_pmr bench_pmr.cpp)
target_link_libraries(bench_pmr PRIVATE cr_base benchmark::benchmark_main)

# W25 — virtual dispatch vs CRTP static polymorphism.
add_executable(bench_crtp bench_crtp.cpp)
target_link_libraries(bench_crtp PRIVATE cr_base benchmark::benchmark_main)
target_include_directories(bench_crtp PRIVATE ${CMAKE_SOURCE_DIR}/phase03_templates/include)
