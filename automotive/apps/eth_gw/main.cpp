// The gateway: CAN frames out onto an Ethernet multicast group.
//
//     vehicle ──► CanFrame ──► tunnel_encode ──► UDP ──► 239.10.0.1:30490
//                                   │                        │
//                              sequence number          eth_sub, eth_sub, ...
//
// Run it with no arguments and it generates the vehicle itself, so the demo
// needs no vcan0 and no sudo. Give it `--can vcan0` and it becomes a real
// gateway, forwarding what week 5's sim_vehicle puts on the bus.
//
//   ./eth_gw                     # generate, publish, no reordering
//   ./eth_gw --reorder 4         # every 4th datagram arrives one tick late
//   ./eth_gw --can vcan0         # forward a real CAN interface instead
//
// --reorder is the interesting one. A real switch reorders under load, rarely
// and unpredictably, which makes the resulting bug nearly impossible to
// reproduce on a desk. So the gateway does it on purpose: hold one datagram
// back, send the next one first, then release it. That is not a lie about the
// network -- it is the same event, made to happen on demand.
//
// The speed generated here only ever climbs. Any decrease the subscriber
// displays therefore came from the network, not from the vehicle.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "av/can/dbc.hpp"
#include "av/can/frame.hpp"
#include "av/can/socket.hpp"
#include "av/can/tunnel.hpp"
#include "av/eth/udp_socket.hpp"
#include "av/log.hpp"

namespace {

namespace log = av::log;

using av::can::CanFrame;
using av::can::CanSocket;
using av::can::DbcDatabase;
using av::can::TunnelHeader;
using av::eth::Endpoint;
using av::eth::SocketStatus;
using av::eth::UdpSocket;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

struct Options {
    std::string group{"239.10.0.1"};
    /// The NIC that outgoing multicast leaves by. 127.0.0.1 keeps the demo on
    /// this machine; on a vehicle this is the vehicle NIC's address, and
    /// getting it wrong is the silent failure of week 7.
    std::string interface_address{"127.0.0.1"};
    std::uint16_t port{30490};  // the port SOME/IP conventionally uses
    std::string can_interface;  // empty: generate instead of forwarding
    std::uint32_t reorder{0};   // 0: off. N: hold back every Nth datagram
    std::uint64_t cycles{0};    // 0: forever
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = (i + 1) < argc;
        if (flag == "--group" && has_value) {
            out.group = argv[++i];
        } else if (flag == "--iface" && has_value) {
            out.interface_address = argv[++i];
        } else if (flag == "--port" && has_value) {
            out.port = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (flag == "--can" && has_value) {
            out.can_interface = argv[++i];
        } else if (flag == "--reorder" && has_value) {
            out.reorder = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (flag == "--cycles" && has_value) {
            out.cycles = std::strtoull(argv[++i], nullptr, 10);
        } else {
            std::cerr << "usage: eth_gw [--group A] [--iface A] [--port P] [--can IF]"
                      << " [--reorder N] [--cycles N]\n";
            return false;
        }
    }
    return true;
}

/// What the "sensors" read, keyed by DBC signal name -- the same contract
/// sim_vehicle uses, so the same DBC drives both.
using Readings = std::map<std::string, double, std::less<>>;

Readings read_sensors(std::uint64_t tick) {
    // Monotonic on purpose, and flat once it reaches the top rather than
    // wrapping: a wrap would look exactly like the bug this demo is about.
    const double speed = std::min(40.0 + (0.5 * static_cast<double>(tick)), 200.0);
    return {
        {"VehicleSpeed", speed},
        {"EngineRpm", 800.0 + (speed * 20.0)},
        {"CoolantTemp", 88.0},
        {"FuelLevel", 60.0},
        {"SteeringAngle", 0.0},
        {"DoorStatus", 0.0},
        {"WarningStatus", 0.0},
    };
}

/// Builds every message the DBC declares from the current readings.
bool generate(const DbcDatabase& database, std::uint64_t tick, std::vector<CanFrame>& frames) {
    const Readings readings = read_sensors(tick);
    frames.clear();

    for (const auto& message : database.messages()) {
        CanFrame frame{};
        frame.id = message.id;
        frame.extended = message.extended;
        frame.length = message.length;

        for (const auto& signal : message.signals) {
            const auto reading = readings.find(signal.name);
            if (reading == readings.end() || !signal.encode(reading->second, frame)) {
                log::error("eth.gw", "cannot build message", "message", message.name, "signal",
                           signal.name);
                return false;
            }
        }
        frames.push_back(frame);
    }
    return !frames.empty();
}

/// Drains everything queued on the CAN socket into `frames`.
void collect(CanSocket& socket, std::vector<CanFrame>& frames) {
    frames.clear();
    while (frames.size() < av::can::kMaxFramesPerDatagram) {
        const auto result = socket.receive();
        if (result.status != av::can::SocketStatus::Ok) {
            return;
        }
        if (result.kind == av::can::FrameKind::Data) {
            frames.push_back(result.frame);
        }
    }
}

std::uint32_t monotonic_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void print_line(std::uint32_t sequence, double speed, const char* what) {
    std::cout << "[tx] seq=" << std::setw(4) << sequence << "  speed=" << std::fixed
              << std::setprecision(1) << std::setw(6) << speed << "  -> " << what << '\n'
              << std::flush;
}

/// Sends one datagram per call and, when asked, holds one back so that the next
/// one overtakes it.
///
/// The delay lives in an object rather than in main's loop because it is one
/// rule with four pieces of state. Spread across loose locals it reads as four
/// unrelated variables; here the invariant -- at most one datagram is ever in
/// flight late -- is visible in one place.
class Publisher {
public:
    /// Takes the socket by value and keeps it. main configures it, then hands
    /// it over: from that point one object owns the fd and the sequence
    /// counter, so there is no way to send a datagram that skips the
    /// numbering.
    Publisher(UdpSocket socket, Endpoint destination, std::uint32_t reorder)
        : socket_(std::move(socket)), destination_(std::move(destination)), reorder_(reorder) {}

    void publish(const std::vector<CanFrame>& frames, double speed) {
        const TunnelHeader header{sequence_, monotonic_ms(),
                                  static_cast<std::uint8_t>(frames.size())};
        if (!av::can::tunnel_encode(header, frames, datagram_)) {
            return;
        }

        if (reorder_ > 0U && !holding_ && (sequence_ % reorder_) == (reorder_ - 1U)) {
            held_ = datagram_;
            held_sequence_ = sequence_;
            holding_ = true;
            print_line(sequence_, speed, "HELD BACK (the switch is busy)");
        } else {
            send(datagram_);
            print_line(sequence_, speed, "sent");
            release();
        }
        ++sequence_;
    }

    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint32_t sequence() const noexcept { return sequence_; }

private:
    void send(const std::vector<std::uint8_t>& bytes) {
        if (socket_.send_to(destination_, bytes.data(), bytes.size()) == SocketStatus::Ok) {
            ++sent_;
        }
    }

    void release() {
        if (!holding_) {
            return;
        }
        send(held_);
        holding_ = false;
        std::cout << "[tx] seq=" << std::setw(4) << held_sequence_
                  << "                 -> sent LATE, out of order\n"
                  << std::flush;
    }

    UdpSocket socket_;
    Endpoint destination_;
    std::uint32_t reorder_;
    std::vector<std::uint8_t> datagram_;
    std::vector<std::uint8_t> held_;
    bool holding_{false};
    std::uint32_t held_sequence_{0};
    std::uint32_t sequence_{0};
    std::uint64_t sent_{0};
};

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
    // Four separate decisions -- see udp_socket.hpp for why none of them is
    // implied by any of the others.
    if (!socket->set_multicast_interface(options.interface_address) ||
        !socket->set_multicast_ttl(1) || !socket->set_multicast_loopback(true) ||
        !socket->set_dscp(46)) {
        log::error("eth.gw", "could not configure the multicast socket");
        return 1;
    }

    // Optional: a real CAN source instead of the built-in one.
    std::optional<CanSocket> can;
    if (!options.can_interface.empty()) {
        can = CanSocket::open(options.can_interface);
        if (!can || !can->set_non_blocking()) {
            return 1;
        }
    }

    const Endpoint destination{options.group, options.port};
    log::info("eth.gw", "publishing", "group", options.group, "port", options.port, "via",
              options.interface_address, "source",
              options.can_interface.empty() ? std::string{"generated"} : options.can_interface,
              "reorder_every", options.reorder);

    std::vector<CanFrame> frames;
    Publisher publisher{std::move(*socket), destination, options.reorder};
    std::uint64_t tick = 0;

    while (g_stop == 0 && (options.cycles == 0U || tick < options.cycles)) {
        bool have_frames = false;
        if (can) {
            collect(*can, frames);
            have_frames = !frames.empty();
        } else {
            have_frames = generate(*database, tick, frames);
        }

        if (have_frames) {
            publisher.publish(frames, read_sensors(tick).at("VehicleSpeed"));
        }

        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    log::info("eth.gw", "stopped", "datagrams_sent", publisher.sent(), "sequence",
              publisher.sequence(), "reason", g_stop != 0 ? "signal" : "cycle limit");
    return 0;
}
