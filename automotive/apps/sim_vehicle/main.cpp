// A vehicle, simulated onto a real CAN interface -- driven by a DBC file.
//
//     cockpit.dbc  ──►  what to send
//                          │
//     sensors  ──►  encode ──►  CanFrame  ──write()──►  vcan0
//
// Week 5 hardcoded the signal table in a header shared with the receiver.
// Week 6 deletes it: the transmitter now reads the same `.dbc` the receiver
// reads, which is how a real project keeps two ECUs in agreement.
//
// Note what disappeared with it. There is no longer a list of messages in this
// file at all. Add a BO_ to the DBC and this program transmits it, provided it
// can supply a value for each of its signals by name.
//
//   ./sim_vehicle                          # vcan0, dbc/cockpit.dbc, forever
//   ./sim_vehicle vcan0 50                 # 50 cycles then exit
//   ./sim_vehicle vcan0 50 other.dbc       # a different database
//
// Watch it with:  candump -tz vcan0

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <thread>

#include "av/can/dbc.hpp"
#include "av/can/frame.hpp"
#include "av/can/socket.hpp"
#include "av/log.hpp"

namespace {

namespace log = av::log;

using av::can::CanFrame;
using av::can::CanSocket;
using av::can::DbcDatabase;
using av::can::SocketStatus;

/// A handler takes no user data, so the flag has nowhere else to live.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// What the "sensors" read at one instant, keyed by DBC signal name.
///
/// Keying by name rather than by position is what lets the DBC drive the
/// program instead of the other way round. A signal the simulator has no value
/// for is reported loudly rather than silently sent as zero.
using Readings = std::map<std::string, double, std::less<>>;

Readings read_sensors(std::uint64_t tick) {
    const double phase = static_cast<double>(tick) * 0.05;
    return {
        {"VehicleSpeed", 60.0 + (40.0 * std::sin(phase))},
        {"EngineRpm", 2000.0 + (800.0 * std::sin(phase * 1.7))},
        {"CoolantTemp", 88.0 + (4.0 * std::sin(phase * 0.3))},
        {"FuelLevel", 60.0 - (static_cast<double>(tick % 120U) * 0.1)},
        {"SteeringAngle", 15.0 * std::sin(phase * 0.8)},
        {"DoorStatus", (tick % 100U) < 5U ? 1.0 : 0.0},
        {"WarningStatus", (tick % 150U) < 10U ? 4.0 : 0.0},
    };
}

/// Builds one message from the DBC and the current readings.
///
/// Returns false if any signal is missing a reading or refuses its value. A
/// frame with one signal missing is worse than no frame: the receiver cannot
/// tell "not sent" from "sent as whatever was already in the buffer".
bool build(const av::can::DbcMessage& message, const Readings& readings, CanFrame& frame) {
    frame = CanFrame{};
    frame.id = message.id;
    frame.extended = message.extended;
    frame.length = message.length;

    for (const auto& signal : message.signals) {
        const auto reading = readings.find(signal.name);
        if (reading == readings.end()) {
            log::error("sim.tx", "no reading for signal", "message", message.name, "signal",
                       signal.name);
            return false;
        }
        if (!signal.encode(reading->second, frame)) {
            // encode() now enforces the DBC's [min|max] as well as the bit
            // width -- the range week 1 parsed past and week 6 finally holds.
            log::error("sim.tx", "encode refused", "signal", signal.name, "value", reading->second,
                       "min", signal.minimum, "max", signal.maximum);
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

    log::info("sim.tx", "transmitting", "interface", interface, "dbc", dbc_path, "messages",
              database->messages().size(), "rate_hz", 10, "cycles",
              limit == 0U ? std::string{"forever"} : std::to_string(limit));

    std::uint64_t tick = 0;
    std::uint64_t sent = 0;

    while (g_stop == 0 && (limit == 0U || tick < limit)) {
        const Readings readings = read_sensors(tick);

        for (const auto& message : database->messages()) {
            CanFrame frame{};
            if (build(message, readings, frame) && socket->send(frame) == SocketStatus::Ok) {
                ++sent;
            }
        }

        if (tick % 10U == 0U) {
            log::info("sim.tx", "vehicle state", "tick", tick, "speed", readings.at("VehicleSpeed"),
                      "rpm", readings.at("EngineRpm"), "steering", readings.at("SteeringAngle"));
        }

        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    log::info("sim.tx", "stopped", "cycles", tick, "frames_sent", sent, "reason",
              g_stop != 0 ? "signal" : "cycle limit");
    return 0;
}
