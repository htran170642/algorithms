// The spine: one vehicle value, all the way down to bytes and back.
//
//     physical value  ->  encode  ->  CAN frame  ->  decode  ->  physical value
//
// Everything the capstone adds later -- a real bus, threads, middleware, QML --
// hangs off this. Run it and watch the numbers survive the trip; then watch
// what happens when the frame or the DBC row is wrong.

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"
#include "av/can/signal.hpp"
#include "av/log.hpp"

namespace {

using av::can::ByteOrder;
using av::can::CanFrame;
using av::can::decode;
using av::can::encode;
using av::can::is_valid;
using av::can::SignalSpec;

namespace log = av::log;

constexpr std::uint32_t kEngineDataId = 0x100U;

/// A signal paired with what the vehicle is currently doing, so the two can
/// never drift out of step the way two parallel arrays would.
struct Measurement {
    SignalSpec spec;
    double value{};
};

// The DBC rows this demo stands in for:
//
//   BO_ 256 EngineData: 8 ECU1
//    SG_ VehicleSpeed  :  0|16@1+ (0.01,0)  "km/h"
//    SG_ EngineRpm     : 23|16@0+ (0.25,0)  "rpm"
//    SG_ CoolantTemp   : 32| 8@1+ (1,-40)   "degC"
//    SG_ SteeringAngle : 40|16@1- (0.1,0)   "deg"
constexpr std::array<Measurement, 4> kEngineData{{
    {{"VehicleSpeed", 0U, 16U, ByteOrder::Intel, false, 0.01, 0.0, "km/h"}, 82.5},
    {{"EngineRpm", 23U, 16U, ByteOrder::Motorola, false, 0.25, 0.0, "rpm"}, 800.0},
    {{"CoolantTemp", 32U, 8U, ByteOrder::Intel, false, 1.0, -40.0, "degC"}, 90.0},
    {{"SteeringAngle", 40U, 16U, ByteOrder::Intel, true, 0.1, 0.0, "deg"}, -12.5},
}};

/// Renders the frame the way `candump vcan0` will in week 5, so the output of
/// this demo and the output of the real tool line up.
std::string candump_line(const CanFrame& frame) {
    std::ostringstream os;
    os << "  vcan0  " << std::uppercase << std::hex << std::setw(3) << std::setfill('0')
       << frame.id << "   [" << std::dec << unsigned{frame.length} << "] ";
    for (std::uint8_t i = 0U; i < frame.length; ++i) {
        os << ' ' << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
           << unsigned{frame.data[i]};
    }
    return os.str();
}

/// Flushes, because the log writes to stderr and this writes to stdout: without
/// it the two streams interleave in whatever order their buffers happen to
/// drain, and the demo reads as nonsense.
void heading(const char* text) { std::cout << '\n' << text << '\n' << std::flush; }

void say(const std::string& line) { std::cout << line << '\n' << std::flush; }

/// Decodes every signal of the message and reports what came back.
///
/// A missing value is logged as a warning rather than substituted with zero:
/// the cluster must be able to tell "not received" from "received as zero".
void decode_all(const CanFrame& frame, const char* what) {
    for (const auto& [spec, measured] : kEngineData) {
        static_cast<void>(measured);  // the receiver never sees the true value
        const auto value = decode(spec, frame);
        if (value) {
            log::info(what, "signal decoded", "name", spec.name, "value", *value, "unit", spec.unit);
        } else {
            log::warn(what, "signal unavailable", "name", spec.name, "reason",
                      "does not fit frame");
        }
    }
}

}  // namespace

int main() {
    av::log::set_level(av::log::Level::Debug);

    // --- transmit side: the vehicle simulator's job --------------------------
    heading("1. Encode measured values into one CAN frame");

    CanFrame frame{};
    frame.id = kEngineDataId;
    frame.length = 8U;

    for (const auto& [spec, measured] : kEngineData) {
        if (!encode(spec, measured, frame)) {
            log::error("tx", "encode refused", "name", spec.name, "value", measured);
            return 1;
        }
        log::debug("tx", "signal encoded", "name", spec.name, "value", measured);
    }

    if (!is_valid(frame)) {
        log::error("tx", "frame is not transmittable", "id", frame.id);
        return 1;
    }

    heading("2. What goes on the wire");
    say(candump_line(frame));

    // --- receive side: what the cockpit does ---------------------------------
    heading("3. Decode it back");
    decode_all(frame, "rx");

    // --- failure 1: the frame is shorter than the DBC says -------------------
    heading("4. Fault: a truncated frame");
    CanFrame truncated = frame;
    truncated.length = 2U;
    say(candump_line(truncated));
    decode_all(truncated, "rx.truncated");

    // --- failure 2: the DBC row is wrong -------------------------------------
    heading("5. Fault: the right bytes read with the wrong byte order");
    SignalSpec swapped = kEngineData.front().spec;
    swapped.order = ByteOrder::Motorola;
    swapped.start_bit = 7U;

    const auto correct = decode(kEngineData.front().spec, frame);
    const auto wrong = decode(swapped, frame);
    if (correct && wrong) {
        log::info("rx.swapped", "same bytes, two readings", "intel", *correct, "motorola", *wrong,
                 "unit", swapped.unit);
        log::warn("rx.swapped", "nothing detects this", "note",
                 "both readings are in range; only a plausibility check would catch it");
    }

    std::cout << '\n' << std::flush;
    return 0;
}
