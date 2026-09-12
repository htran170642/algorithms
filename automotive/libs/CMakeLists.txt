# Reusable libraries. Each one is added as the roadmap reaches it.
#
#   av_log      week 0   structured logging
#   av_can      week 1   bit walking, CanFrame, signal decode    <- here
#               week 5   ... plus SocketCAN
#               week 6   ... plus a DBC parser
#   av_conc     week 2   BoundedQueue -- between threads
#   av_ipc      week 3   epoll, Unix domain socket, shared memory  <- here
#   av_eth      week 7   UDP + IPv4 multicast -- the automotive Ethernet link
#   av_service  week 8   mini SOME/IP + VehicleService
#               week 9   ... plus Service Discovery
#   av_mw       week 10  the middleware: Runtime, Proxy, Skeleton
#
# av_conc and av_ipc stay separate on purpose: one crosses a thread boundary,
# the other crosses an address space. Conflating them hides the difference that
# actually matters when something goes wrong.
#
# av_can arrives in week 1 rather than week 4 because the roadmap builds the
# spine first (CLAUDE.md section 14). Bit/endianness helpers live in it rather
# than a separate av_core: outside a CAN payload they have no meaning.

# av_ipc is listed before av_can because week 5 gives av_can a SocketCAN
# endpoint that reuses av_ipc's UniqueFd rather than growing a second one.
add_subdirectory(av_log)
add_subdirectory(av_conc)
add_subdirectory(av_ipc)
add_subdirectory(av_can)

# av_eth reuses av_ipc's UniqueFd too. It stays separate from av_can because
# the two answer different questions: av_can is *what a signal means*, av_eth
# is *how bytes reach another ECU*. Week 8's SOME/IP sits on top of av_eth.
add_subdirectory(av_eth)

# av_service depends on neither av_eth nor av_can, and that is the point: the
# SOME/IP header is a byte layout, not a transport and not a signal catalogue.
# Week 9 is where Service Discovery finally binds a service to an endpoint.
add_subdirectory(av_service)

# av_mw is the first library that joins the others: it links av_service
# PUBLIC (its headers name service ids) and av_eth + av_ipc PRIVATE (they do
# not). An application links av_mw and nothing that opens a socket.
add_subdirectory(av_mw)
