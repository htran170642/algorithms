// A vehicle, simulated onto a real CAN interface.
//
//     sim_vehicle  --encode-->  CanFrame  --write()-->  vcan0
//
// This is the left-hand end of the capstone chain in CLAUDE.md section 7. It
// exists so the receiver has something to receive that is not a unit test: a
// bus that ticks along at 10 Hz whether or not anyone is listening, which is
// exactly how a real ECU behaves.
//
//   ./sim_vehicle                 # vcan0, forever, Ctrl-C to stop
//   ./sim_vehicle vcan0 50        # 50 cycles then exit
//
// Watch it from another terminal with the tool the rest of the industry uses:
//   candump -tz vcan0

#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

#include "av/can/frame.hpp"
#include "av/can/signal.hpp"
#include "av/can/socket.hpp"
#include "av/log.hpp"
#include "vehicle_signals.hpp"

namespace {

namespace log = av::log;

using av::can::CanFrame;
using av::can::CanSocket;
using av::can::encode;
using av::can::SocketStatus;

/// Set from the signal handler. `volatile sig_atomic_t` is the only type the C
/// standard promises is safe to touch from a handler -- a bool or an int is
/// not, however well it happens to work in practice.
///
/// It has to be a mutable global: a handler takes no user data, so there is
/// nowhere else for the flag to live. This is the one case where the guideline
/// against non-const globals has no alternative to offer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// The physical values at one instant. A real ECU reads these from sensors;
/// here they are a slow sine so the receiver shows something moving.
struct VehicleState {
    double speed{};
    double rpm{};
    double coolant{};
    double fuel{};
    double steering{};
    double doors{};
    double warnings{};
};

VehicleState state_at(std::uint64_t tick) {
    const double phase = static_cast<double>(tick) * 0.05;
    VehicleState state{};
    state.speed = 60.0 + (40.0 * std::sin(phase));         // 20..100 km/h
    state.rpm = 2000.0 + (800.0 * std::sin(phase * 1.7));  // 1200..2800 rpm
    state.coolant = 88.0 + (4.0 * std::sin(phase * 0.3));  // 84..92 degC
    state.fuel = 60.0 - (static_cast<double>(tick % 120U) * 0.1);
    state.steering = 15.0 * std::sin(phase * 0.8);  // -15..15 deg
    state.doors = (tick % 100U) < 5U ? 1.0 : 0.0;   // driver door blips
    state.warnings = (tick % 150U) < 10U ? 4.0 : 0.0;
    return state;
}

/// Encodes one message. Returns false if any signal refuses -- an out-of-range
/// value is a transmitter bug, and shipping a frame with one signal missing is
/// worse than shipping nothing.
template <typename Table, typename Values>
bool build(std::uint32_t id, const Table& table, const Values& values, CanFrame& frame) {
    frame = CanFrame{};
    frame.id = id;
    frame.length = av::demo::kMessageLength;

    for (std::size_t i = 0; i < table.size(); ++i) {
        if (!encode(table[i], values[i], frame)) {
            log::error("sim.tx", "encode refused", "signal", table[i].name, "value", values[i]);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    log::set_level(log::Level::Info);

    const std::string interface = (argc > 1) ? argv[1] : "vcan0";
    const std::uint64_t limit = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0U;

    // signal() returns the previous handler; there is nothing useful to do
    // with it here, and discarding it silently is what cert-err33-c objects to.
    static_cast<void>(std::signal(SIGINT, on_signal));
    static_cast<void>(std::signal(SIGTERM, on_signal));

    auto socket = CanSocket::open(interface);
    if (!socket) {
        return 1;
    }
    log::info("sim.tx", "transmitting", "interface", interface, "rate_hz", 10, "cycles",
              limit == 0U ? std::string{"forever"} : std::to_string(limit));

    std::uint64_t tick = 0;
    std::uint64_t sent = 0;

    while (g_stop == 0 && (limit == 0U || tick < limit)) {
        const VehicleState state = state_at(tick);
        const std::array<double, 4> engine{state.speed, state.rpm, state.coolant, state.fuel};
        const std::array<double, 3> body{state.steering, state.doors, state.warnings};

        CanFrame frame{};
        if (build(av::demo::kEngineDataId, av::demo::kEngineData, engine, frame) &&
            socket->send(frame) == SocketStatus::Ok) {
            ++sent;
        }
        if (build(av::demo::kBodyStateId, av::demo::kBodyState, body, frame) &&
            socket->send(frame) == SocketStatus::Ok) {
            ++sent;
        }

        if (tick % 10U == 0U) {
            log::info("sim.tx", "vehicle state", "tick", tick, "speed", state.speed, "rpm",
                      state.rpm, "steering", state.steering);
        }

        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    log::info("sim.tx", "stopped", "cycles", tick, "frames_sent", sent, "reason",
              g_stop != 0 ? "signal" : "cycle limit");
    return 0;
}
