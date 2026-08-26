# Reusable libraries. Each one is added as the roadmap reaches it.
#
#   av_log      week 0   structured logging
#   av_can      week 1   bit walking, CanFrame, signal decode    <- here
#               week 5   ... plus SocketCAN
#               week 6   ... plus a DBC parser
#   av_ipc      week 3   queue, Unix domain socket, shared memory
#   av_service  week 8   mini SOME/IP + VehicleService
#
# av_can arrives in week 1 rather than week 4 because the roadmap builds the
# spine first (CLAUDE.md section 14). Bit/endianness helpers live in it rather
# than a separate av_core: outside a CAN payload they have no meaning.

add_subdirectory(av_log)
add_subdirectory(av_can)
