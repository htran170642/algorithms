# Runnable demos. Each week that produces something worth watching gets one.
#
#   spine     week 1   physical value -> CAN frame -> decoded value, plus faults
#   cockpit   week 2   rx / decode / ui threads, backpressure, graceful shutdown
#   ipc_bench week 3   unix socket vs shared memory round-trip latency

add_subdirectory(spine)
add_subdirectory(cockpit)
add_subdirectory(ipc_bench)
