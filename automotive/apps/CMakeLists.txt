# Runnable demos. Each week that produces something worth watching gets one.
#
#   spine     week 1   physical value -> CAN frame -> decoded value, plus faults
#   cockpit   week 2   rx / decode / ui threads, backpressure, graceful shutdown
#   ipc_bench week 3   unix socket vs shared memory round-trip latency
#   can_bus   week 4   arbitration bit by bit, and one ECU driven to bus-off
#   sim_vehicle week 5 encodes the seven signals onto a real vcan0 interface
#   can_rx    week 5   epoll + PF_CAN + decode, with staleness detection
#             week 6   ... both now driven by dbc/cockpit.dbc
#
# common/ held the SignalSpec table these two shared. Week 6 deleted it: they
# read the same .dbc file instead, which is how a real project keeps two ECUs
# in agreement.

add_subdirectory(spine)
add_subdirectory(cockpit)
add_subdirectory(ipc_bench)
add_subdirectory(can_bus)
add_subdirectory(sim_vehicle)
add_subdirectory(can_rx)

# Week 7. eth_gw generates (or forwards) CAN frames onto a multicast group;
# eth_sub joins it and decodes them with the same DBC. Run eth_gw --reorder 4
# against eth_sub and then against eth_sub --naive: the difference on screen is
# the whole reason a sequence number exists.
add_subdirectory(eth_gw)
add_subdirectory(eth_sub)
