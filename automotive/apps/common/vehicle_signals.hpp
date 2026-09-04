#pragma once

// The signal catalogue the week-5 demos share.
//
// CLAUDE.md section 7 lists seven required signals. Here they are, laid out
// across two messages, written the way a DBC file would write them:
//
//   BO_ 256 EngineData: 8 ECU_Powertrain
//    SG_ VehicleSpeed  :  0|16@1+ (0.01,0)  "km/h"
//    SG_ EngineRpm     : 16|16@1+ (0.25,0)  "rpm"
//    SG_ CoolantTemp   : 32| 8@1+ (1,-40)   "degC"
//    SG_ FuelLevel     : 40| 8@1+ (0.5,0)   "%"
//
//   BO_ 512 BodyState: 8 ECU_Body
//    SG_ SteeringAngle :  0|16@1- (0.1,0)   "deg"
//    SG_ DoorStatus    : 16| 4@1+ (1,0)     "bitfield"
//    SG_ WarningStatus : 24| 8@1+ (1,0)     "bitfield"
//
// This lives in apps/ and not in libs/ on purpose. It is a stand-in for a file
// that does not exist yet: week 6 builds the DBC parser, and this table then
// becomes test data rather than source code. Putting it in the library now
// would mean deleting it from the library later.
//
// One table, included by both the transmitter and the receiver, so the two
// cannot drift apart. As separate literals, a change on one side produces a
// plausible wrong number on the other -- week 1's silent fault, reintroduced
// by copy-paste.

#include <array>
#include <cstdint>

#include "av/can/bit.hpp"
#include "av/can/signal.hpp"

namespace av::demo {

using av::can::ByteOrder;
using av::can::SignalSpec;

inline constexpr std::uint32_t kEngineDataId = 0x100;
inline constexpr std::uint32_t kBodyStateId = 0x200;
inline constexpr std::uint8_t kMessageLength = 8;

inline constexpr std::array<SignalSpec, 4> kEngineData{{
    {"VehicleSpeed", 0U, 16U, ByteOrder::Intel, false, 0.01, 0.0, "km/h"},
    {"EngineRpm", 16U, 16U, ByteOrder::Intel, false, 0.25, 0.0, "rpm"},
    {"CoolantTemp", 32U, 8U, ByteOrder::Intel, false, 1.0, -40.0, "degC"},
    {"FuelLevel", 40U, 8U, ByteOrder::Intel, false, 0.5, 0.0, "%"},
}};

inline constexpr std::array<SignalSpec, 3> kBodyState{{
    {"SteeringAngle", 0U, 16U, ByteOrder::Intel, true, 0.1, 0.0, "deg"},
    {"DoorStatus", 16U, 4U, ByteOrder::Intel, false, 1.0, 0.0, "bitfield"},
    {"WarningStatus", 24U, 8U, ByteOrder::Intel, false, 1.0, 0.0, "bitfield"},
}};

}  // namespace av::demo
