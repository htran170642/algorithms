# Reusable libraries. Each one is added as the roadmap reaches it.
#
#   av_log      week 0   structured logging          <- here
#   av_core     week 1   bit / endianness helpers
#   av_ipc      week 3   queue, Unix domain socket, shared memory
#   av_can      week 4   CanFrame, DBC-like decoder, SocketCAN
#   av_service  week 8   mini SOME/IP + VehicleService

add_subdirectory(av_log)
