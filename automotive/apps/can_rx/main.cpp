// The receiver: a CAN socket inside week 3's epoll loop.
//
//     vcan0 --read()--> CanFrame --decode()--> physical value
//       |
//     Poller (epoll)  <- unchanged from week 3; a CAN socket is just an fd
//
// Three things this demo is really about:
//
//   1. **Kernel-side filtering.** The socket asks for 0x100 and 0x200 only.
//      Everything else is dropped before it is ever copied to this process.
//   2. **Timeout as a first-class state.** A signal that stops arriving is not
//      the same as a signal reading zero. The receiver must be able to say
//      "stale" -- CLAUDE.md section 7's `Fault -> Detection -> ... -> Safe
//      State`, at the earliest point in the chain where it is detectable.
//   3. **Bus errors are readable.** With CAN_RAW_ERR_FILTER on, week 4's TEC
//      and REC arrive through this same loop. The bus-off that ends week 4 is
//      no longer silent here.
//
//   ./can_rx                # vcan0, forever
//   ./can_rx vcan0 40       # stop after 40 data frames

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "av/can/frame.hpp"
#include "av/can/signal.hpp"
#include "av/can/socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"
#include "vehicle_signals.hpp"

namespace {

namespace log = av::log;

using av::can::CanFilter;
using av::can::CanFrame;
using av::can::CanSocket;
using av::can::decode;
using av::can::FrameKind;
using av::can::SocketStatus;
using av::ipc::Poller;

/// A handler takes no user data, so the flag has nowhere else to live. This is
/// the one case where the guideline against non-const globals offers no
/// alternative.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// How long a signal may go unheard before it is reported stale. The
/// transmitter runs at 10 Hz, so 500 ms is five missed cycles -- late enough
/// not to fire on jitter, early enough that a driver would not yet have acted
/// on the stale value.
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

/// Decodes every signal of whichever message this is.
///
/// A signal that does not fit is logged as unavailable rather than substituted
/// with zero. Week 1's rule, still holding: absence is not zero.
template <typename Table>
void report(const Table& table, const CanFrame& frame) {
    for (const auto& spec : table) {
        const auto value = decode(spec, frame);
        if (value) {
            log::info("can.rx", "signal", "name", spec.name, "value", *value, "unit", spec.unit);
        } else {
            log::warn("can.rx", "signal unavailable", "name", spec.name, "reason",
                      "does not fit this frame");
        }
    }
}

/// What the loop knows between iterations.
struct ReceiverState {
    std::uint64_t received{0};
    bool stale{false};
    std::chrono::steady_clock::time_point last_frame{std::chrono::steady_clock::now()};
};

/// Reads everything queued on the socket, then returns.
///
/// Level-triggered epoll forgives a partial drain, but draining fully here
/// keeps one busy sender from starving the staleness check in the outer loop.
void drain(CanSocket& socket, const std::string& interface, ReceiverState& state) {
    while (true) {
        const auto result = socket.receive();
        if (result.status != SocketStatus::Ok) {
            return;  // WouldBlock: queue empty. Error: already logged.
        }

        if (result.kind == FrameKind::BusError) {
            // The evidence week 4 said to look for, arriving by itself.
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

        if (result.frame.id == av::demo::kEngineDataId) {
            report(av::demo::kEngineData, result.frame);
        } else if (result.frame.id == av::demo::kBodyStateId) {
            report(av::demo::kBodyState, result.frame);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    log::set_level(log::Level::Info);

    const std::string interface = (argc > 1) ? argv[1] : "vcan0";
    const std::uint64_t limit = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0U;

    // signal() returns the previous handler; nothing useful to do with it here.
    static_cast<void>(std::signal(SIGINT, on_signal));
    static_cast<void>(std::signal(SIGTERM, on_signal));

    auto socket = CanSocket::open(interface);
    if (!socket) {
        return 1;
    }
    if (!socket->set_non_blocking()) {
        log::error("can.rx", "could not set non-blocking");
        return 1;
    }

    // Week 4's counters, delivered as ordinary reads. Off by default, which is
    // why so much production code never notices a controller degrading.
    static_cast<void>(socket->enable_error_frames());

    // Two ids, exact match. Everything else the bus carries is discarded in
    // the kernel and never wakes this process at all.
    if (!socket->set_filters({
            CanFilter{av::demo::kEngineDataId, av::can::kStandardIdMask, false},
            CanFilter{av::demo::kBodyStateId, av::can::kStandardIdMask, false},
        })) {
        return 1;
    }

    auto poller = Poller::create();
    if (!poller || !poller->watch_readable(socket->fd())) {
        log::error("can.rx", "could not set up epoll");
        return 1;
    }

    log::info("can.rx", "listening", "interface", interface, "filters", 2, "stale_after_ms",
              kStaleAfter.count());

    ReceiverState state;

    while (g_stop == 0 && (limit == 0U || state.received < limit)) {
        // A CAN socket is an ordinary descriptor: the week-3 loop is unchanged.
        const auto& events = poller->wait(std::chrono::milliseconds{100});

        for (const auto& event : events) {
            if (event.readable) {
                drain(*socket, interface, state);
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
