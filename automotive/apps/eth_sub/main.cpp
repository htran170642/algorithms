// The subscriber: joins the multicast group and decodes what the gateway sends.
//
//     239.10.0.1:30490 ──► UDP ──► tunnel_decode ──► CanFrame ──► DBC ──► km/h
//                                       │
//                                 SequenceTracker  <- the whole point of week 7
//
//   ./eth_sub            # uses the sequence number: stale datagrams dropped
//   ./eth_sub --naive    # ignores it: applies whatever arrives, in any order
//
// Run the gateway with `--reorder 4` and run this twice, once each way. The
// code path that differs is three lines long, and the difference on screen is
// a speed that climbs versus a speed that jumps backwards.
//
// That is the lesson. On CAN the ordering came from the bus and no application
// could get it wrong. On Ethernet ordering is an application concern, and a
// program that simply trusts arrival order is not obviously broken -- it logs
// nothing, crashes never, and is wrong.

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "av/can/dbc.hpp"
#include "av/can/tunnel.hpp"
#include "av/eth/udp_socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"

namespace {

namespace log = av::log;

using av::can::DbcDatabase;
using av::can::PacketOrder;
using av::can::SequenceTracker;
using av::eth::SocketStatus;
using av::eth::UdpSocket;
using av::ipc::Poller;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

struct Options {
    std::string group{"239.10.0.1"};
    /// Which NIC joins the group. "0.0.0.0" lets the kernel pick, which is
    /// right on a laptop and wrong on an ECU with two NICs -- the join lands on
    /// one and the traffic arrives on the other, in complete silence.
    std::string interface_address{"127.0.0.1"};
    std::uint16_t port{30490};
    bool naive{false};  ///< ignore the sequence number, believe arrival order
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = (i + 1) < argc;
        if (flag == "--naive") {
            out.naive = true;
        } else if (flag == "--group" && has_value) {
            out.group = argv[++i];
        } else if (flag == "--iface" && has_value) {
            out.interface_address = argv[++i];
        } else if (flag == "--port" && has_value) {
            out.port = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else {
            std::cerr << "usage: eth_sub [--group A] [--iface A] [--port P] [--naive]\n";
            return false;
        }
    }
    return true;
}

struct Display {
    double speed{0.0};
    bool has_speed{false};
    std::uint64_t backwards{0};  ///< times the displayed speed fell
};

/// Pulls VehicleSpeed out of a decoded batch, using the same DBC the gateway
/// encoded it with. Nothing about the tunnel format knows what a signal means.
void apply(const DbcDatabase& database, const av::can::TunnelPacket& packet, Display& display,
           const char* note) {
    for (const auto& frame : packet.frames) {
        const auto* message = database.find(frame.id, frame.extended);
        if (message == nullptr) {
            continue;
        }
        for (const auto& signal : message->signals) {
            if (signal.name != "VehicleSpeed") {
                continue;
            }
            const auto value = signal.decode(frame);
            if (!value) {
                continue;
            }

            const bool fell = display.has_speed && *value < display.speed;
            if (fell) {
                ++display.backwards;
            }
            display.speed = *value;
            display.has_speed = true;

            std::cout << "[rx] seq=" << std::setw(4) << packet.header.sequence
                      << "  speed=" << std::fixed << std::setprecision(1) << std::setw(6) << *value
                      << "  " << note;
            if (fell) {
                std::cout << "   <<< SPEED WENT BACKWARDS";
            }
            std::cout << '\n' << std::flush;
        }
    }
}

/// Decides what to do with one datagram.
void handle(const std::vector<std::uint8_t>& buffer, std::size_t size, const DbcDatabase& database,
            SequenceTracker& tracker, const Options& options, Display& display) {
    const auto packet = av::can::tunnel_decode(buffer.data(), size);
    if (!packet) {
        log::warn("eth.sub", "not a tunnel datagram -- ignored", "bytes", size);
        return;
    }

    const PacketOrder order = tracker.observe(packet->header.sequence);

    // The three lines that separate a correct receiver from a plausible one.
    if (!options.naive && (order == PacketOrder::Stale || order == PacketOrder::Duplicate)) {
        std::cout << "[rx] seq=" << std::setw(4) << packet->header.sequence
                  << "                 STALE -- dropped (an older reading arrived late)\n"
                  << std::flush;
        return;
    }

    const char* note = "";
    if (order == PacketOrder::Gap) {
        note = "** datagram(s) LOST **";
    } else if (order == PacketOrder::Stale || order == PacketOrder::Duplicate) {
        note = "** stale, applied anyway (--naive) **";
    }
    apply(database, *packet, display, note);
}

}  // namespace

int main(int argc, char** argv) {
    log::set_level(log::Level::Info);

    Options options;
    if (!parse_options(argc, argv, options)) {
        return 2;
    }

    static_cast<void>(std::signal(SIGINT, on_signal));
    static_cast<void>(std::signal(SIGTERM, on_signal));

    const auto database = DbcDatabase::load(AV_DBC_PATH);
    if (!database) {
        return 1;
    }

    auto socket = UdpSocket::open();
    if (!socket) {
        return 1;
    }
    if (!socket->set_non_blocking()) {
        log::error("eth.sub", "could not set non-blocking");
        return 1;
    }
    // bind() before join(): the membership is added to a socket that already
    // owns the port, and SO_REUSEADDR inside bind_any() is what lets a second
    // subscriber run on this same machine.
    if (!socket->bind_any(options.port) ||
        !socket->join_multicast(options.group, options.interface_address)) {
        return 1;
    }

    auto poller = Poller::create();
    if (!poller) {
        log::error("eth.sub", "could not create epoll");
        return 1;
    }

    UdpSocket& udp = *socket;
    const DbcDatabase& db = *database;
    Poller& epoll = *poller;

    if (!epoll.watch_readable(udp.fd())) {
        log::error("eth.sub", "could not watch the udp socket");
        return 1;
    }

    log::info("eth.sub", "listening", "group", options.group, "port", options.port, "via",
              options.interface_address, "mode",
              options.naive ? "NAIVE (trusts arrival order)" : "sequence-checked");

    SequenceTracker tracker;
    Display display;
    std::vector<std::uint8_t> buffer;

    while (g_stop == 0) {
        const auto& events = epoll.wait(std::chrono::milliseconds{200});
        for (const auto& event : events) {
            if (!event.readable) {
                continue;
            }
            while (true) {
                const auto result = udp.receive(buffer);
                if (result.status != SocketStatus::Ok) {
                    break;  // WouldBlock: queue empty. Error: already logged.
                }
                handle(buffer, result.size, db, tracker, options, display);
            }
        }
    }

    std::cout << "\n---------------------------------------------\n";
    std::cout << "accepted datagrams  : " << tracker.accepted() << '\n';
    std::cout << "lost in transit     : " << tracker.lost() << '\n';
    std::cout << "stale / duplicate   : " << tracker.stale() << '\n';
    std::cout << "speed went backwards: " << display.backwards
              << (options.naive ? "   <- what ignoring the sequence number costs\n"
                                : "   <- zero, because stale datagrams were dropped\n");
    std::cout << std::flush;
    return 0;
}
