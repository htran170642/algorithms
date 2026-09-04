# Runnable demos. Each week that produces something worth watching gets one.
#
#   spine     week 1   physical value -> CAN frame -> decoded value, plus faults
#   cockpit   week 2   rx / decode / ui threads, backpressure, graceful shutdown
#   ipc_bench week 3   unix socket vs shared memory round-trip latency
#   can_bus   week 4   arbitration bit by bit, and one ECU driven to bus-off
#   sim_vehicle week 5 encodes the seven signals onto a real vcan0 interface
#   can_rx    week 5   epoll + PF_CAN + decode, with staleness detection
#
# common/ is not a demo: it is the SignalSpec table sim_vehicle and can_rx
# share, so the two cannot drift apart. Week 6 replaces it with a .dbc file.

add_subdirectory(common)

add_subdirectory(spine)
add_subdirectory(cockpit)
add_subdirectory(ipc_bench)
add_subdirectory(can_bus)
add_subdirectory(sim_vehicle)
add_subdirectory(can_rx)
