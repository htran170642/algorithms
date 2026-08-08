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

# W30 — object pool free-list vs new/delete under create/destroy churn.
add_executable(bench_pool bench_pool.cpp)
target_link_libraries(bench_pool PRIVATE cr_base benchmark::benchmark_main)
target_include_directories(bench_pool PRIVATE ${CMAKE_SOURCE_DIR}/phase04_memory/include)

# W31 — cache locality: array-of-structs vs struct-of-arrays streaming few fields.
add_executable(bench_aos_soa bench_aos_soa.cpp)
target_link_libraries(bench_aos_soa PRIVATE cr_base benchmark::benchmark_main)

# W31 — false sharing: adjacent hot counters vs one-per-cache-line.
# std::thread needs the platform thread library; Phase 5 will lean on this too.
find_package(Threads REQUIRED)
add_executable(bench_false_sharing bench_false_sharing.cpp)
target_link_libraries(bench_false_sharing PRIVATE cr_base benchmark::benchmark_main Threads::Threads)

# W41 — benchmarking discipline on a REAL component: SpscRing throughput
# (items/sec, UseRealTime), and alignas(64) padding measured on the ring itself.
add_executable(bench_spsc_ring bench_spsc_ring.cpp)
target_link_libraries(bench_spsc_ring PRIVATE cr_base benchmark::benchmark_main Threads::Threads)
