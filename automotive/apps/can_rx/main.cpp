// The receiver: a CAN socket, week 3's epoll loop, and a DBC file.
//
//     cockpit.dbc ──►  what to expect, and what it means
//                              │
//     vcan0 ──read()──► CanFrame ──decode()──► physical value + name
//       │
//     Poller (epoll)  <- unchanged from week 3; a CAN socket is just an fd
//
// Week 6 changes three things about the receiver:
//
//   1. **The filter set comes from the DBC.** Whatever messages the database
//      declares are the messages the kernel is asked for. Nothing hardcoded.
//   2. **Range violations are visible.** decode() still returns an
//      out-of-range value -- it is a fact about the bus -- and the receiver
//      logs it as a fault rather than hiding it.
//   3. **Enumerations are named.** A DoorStatus of 1 prints as "DriverOpen",
//      because the DBC's VAL_ table says so.
//
//   ./can_rx                          # vcan0, dbc/cockpit.dbc
//   ./can_rx vcan0 40                 # stop after 40 data frames
//   ./can_rx vcan0 40 other.dbc

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "av/can/dbc.hpp"
#include "av/can/frame.hpp"
#include "av/can/socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"

namespace {

namespace log = av::log;

using av::can::CanFilter;
using av::can::CanFrame;
using av::can::CanSocket;
using av::can::DbcDatabase;
using av::can::FrameKind;
using av::can::SocketStatus;
using av::ipc::Poller;

/// A handler takes no user data, so the flag has nowhere else to live.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// How long a signal may go unheard before it is reported stale. The
/// transmitter runs at 10 Hz, so 500 ms is five missed cycles.
constexpr auto kStaleAfter = std::chrono::milliseconds{500};

/// Renders the frame the way `candump vcan0` does, so this output and the
/// standard tool can be compared line for line.
std::string candump_line(const std::string& interface, const CanFrame& frame) {
    std::ostringstream os;
    os << "  " << interface << "  " << std::uppercase << std::hex
       << std::setw(frame.extended ? 8 : 3) << std::setfill('0') << frame.id << "   [" << std::dec
       << unsigned{frame.length} << "] ";
    for (std::uint8_t i = 0; i < frame.length; ++i) {
        os << ' ' << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << unsigned{frame.data[i]};
    }
    return os.str();
}

/// Decodes and reports every signal the DBC declares for this message.
///
/// Three outcomes, three different log lines. Collapsing them would lose the
/// distinction that matters when something goes wrong.
void report(const av::can::DbcMessage& message, const CanFrame& frame) {
    for (const auto& signal : message.signals) {
        const auto value = signal.decode(frame);
        if (!value) {
            log::warn("can.rx", "signal unavailable", "message", message.name, "signal",
                      signal.name, "reason", "does not fit this frame");
            continue;
        }
        if (!signal.in_range(*value)) {
            // Not dropped, not clamped: reported. The DBC says this cannot
            // happen, and it just did.
            log::error("can.rx", "signal out of range", "signal", signal.name, "value", *value,
                       "min", signal.minimum, "max", signal.maximum);
            continue;
        }
        const auto name = signal.value_name(*value);
        if (name.empty()) {
            log::info("can.rx", "signal", "name", signal.name, "value", *value, "unit",
                      signal.unit);
        } else {
            log::info("can.rx", "signal", "name", signal.name, "value", *value, "means", name);
        }
    }
}

struct ReceiverState {
    std::uint64_t received{0};
    bool stale{false};
    std::chrono::steady_clock::time_point last_frame{std::chrono::steady_clock::now()};
};

/// Reads everything queued on the socket, then returns.
void drain(CanSocket& socket, const std::string& interface, const DbcDatabase& database,
           ReceiverState& state) {
    while (true) {
        const auto result = socket.receive();
        if (result.status != SocketStatus::Ok) {
            return;  // WouldBlock: queue empty. Error: already logged.
        }

        if (result.kind == FrameKind::BusError) {
            log::error("can.rx", "bus error", "classes", result.error.classes, "tec",
                       unsigned{result.error.transmit_errors}, "rec",
                       unsigned{result.error.receive_errors}, "bus_off", result.error.bus_off,
                       "passive", result.error.error_passive, "warning",
                       result.error.error_warning);
            continue;
        }

        state.last_frame = std::chrono::steady_clock::now();
        if (state.stale) {
            log::info("can.rx", "signals live again", "after_frames", state.received);
            state.stale = false;
        }
        ++state.received;

        std::cout << candump_line(interface, result.frame) << '\n' << std::flush;

        const auto* message = database.find(result.frame.id, result.frame.extended);
        if (message == nullptr) {
            // Reachable only if the filters were widened by hand. Worth a line:
            // an unknown id on a bus you thought you knew is information.
            log::warn("can.rx", "no DBC entry for this id", "id", result.frame.id);
            continue;
        }
        if (result.frame.length < message->length) {
            log::warn("can.rx", "frame shorter than the DBC says", "message", message->name,
                      "expected", unsigned{message->length}, "got", unsigned{result.frame.length});
        }
        report(*message, result.frame);
    }
}

}  // namespace

int main(int argc, char** argv) {
    log::set_level(log::Level::Info);

    const std::string interface = (argc > 1) ? argv[1] : "vcan0";
    const std::uint64_t limit = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0U;
    const std::string dbc_path = (argc > 3) ? argv[3] : AV_DBC_PATH;

    static_cast<void>(std::signal(SIGINT, on_signal));
    static_cast<void>(std::signal(SIGTERM, on_signal));

    const auto database = DbcDatabase::load(dbc_path);
    if (!database) {
        return 1;
    }

    auto socket = CanSocket::open(interface);
    if (!socket) {
        return 1;
    }
    if (!socket->set_non_blocking()) {
        log::error("can.rx", "could not set non-blocking");
        return 1;
    }

    // Week 4's counters, delivered as ordinary reads.
    static_cast<void>(socket->enable_error_frames());

    // The DBC decides what this receiver listens to. Everything else the bus
    // carries is discarded in the kernel and never wakes this process.
    std::vector<CanFilter> filters;
    filters.reserve(database->messages().size());
    for (const auto& message : database->messages()) {
        filters.push_back(CanFilter{
            message.id,
            message.extended ? av::can::kExtendedIdMask : av::can::kStandardIdMask,
            message.extended});
    }
    if (!socket->set_filters(filters)) {
        return 1;
    }

    auto poller = Poller::create();
    if (!poller) {
        log::error("can.rx", "could not create epoll");
        return 1;
    }

    // Bind once, after the checks. Dereferencing the optionals inside the loop
    // is what the unchecked-optional-access analysis rightly objects to: it
    // cannot see that an early return already guaranteed they hold a value,
    // and a `||` short-circuit hides it further.
    CanSocket& can = *socket;
    const DbcDatabase& db = *database;
    Poller& epoll = *poller;

    if (!epoll.watch_readable(can.fd())) {
        log::error("can.rx", "could not watch the CAN socket");
        return 1;
    }

    log::info("can.rx", "listening", "interface", interface, "dbc", dbc_path, "filters",
              filters.size(), "stale_after_ms", kStaleAfter.count());

    ReceiverState state;

    while (g_stop == 0 && (limit == 0U || state.received < limit)) {
        const auto& events = epoll.wait(std::chrono::milliseconds{100});

        for (const auto& event : events) {
            if (event.readable) {
                drain(can, interface, db, state);
            }
        }

        // Detection, not just reception. Nothing arriving is itself a fact, and
        // it is the only thing that distinguishes a dead transmitter from a
        // quiet one.
        if (!state.stale && std::chrono::steady_clock::now() - state.last_frame > kStaleAfter) {
            state.stale = true;
            log::warn("can.rx", "no frames -- signals are stale", "timeout_ms", kStaleAfter.count(),
                      "fallback", "hold last value, mark invalid");
        }
    }

    log::info("can.rx", "stopped", "frames", state.received, "reason",
              g_stop != 0 ? "signal" : "frame limit");
    return 0;
}
