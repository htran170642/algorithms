# Google Benchmark harness. Comes online properly in Phase 6 (W41), but exists
# from Week 0 so that any earlier week CAN measure instead of guess — the
# list-vs-vector week (W13) and the false-sharing week (W31) both need it.
#
# Always benchmark the RELEASE preset. Benchmarking a -O0 debug build measures
# nothing but the absence of the optimizer.

add_executable(bench_all bench_all.cpp)
target_link_libraries(bench_all PRIVATE cr_base benchmark::benchmark_main)
