#pragma once

// The VehicleService interface definition.
//
// In a real project this file is *generated* -- from ARXML in AUTOSAR
// Adaptive, or from a Franca .fidl with vsomeip. Nobody types service ids by
// hand, because the client and the server are built by different teams and
// often different companies, and the one thing they must agree on to the bit
// is exactly this.
//
// It lives in av_service rather than in an apps/common/ header on purpose.
// Week 6 deleted apps/common because it duplicated what the DBC already said
// (see apps/CMakeLists.txt). This is the opposite case: nothing else in the
// repository states these numbers, so this file *is* the contract.
//
//     VehicleService 0x1234
//     ├── GetSpeed()          0x0001   request  -> response
//     ├── GetRpm()            0x0002   request  -> response
//     └── OnSpeedChanged      0x8001   notification, pushed to subscribers
//
// The two halves travel differently, and that difference is the week's point:
//
//     methods  unicast   client -> server -> client   (one asks, one answers)
//     events   multicast server -> group              (nobody asks)

#include <cstdint>

namespace av::service::vehicle {

inline constexpr std::uint16_t kServiceId = 0x1234;

/// Bumped when the interface changes incompatibly. A server answers
/// WrongInterfaceVersion rather than guessing what an older client meant.
inline constexpr std::uint8_t kInterfaceVersion = 1;

inline constexpr std::uint16_t kGetSpeed = 0x0001;
inline constexpr std::uint16_t kGetRpm = 0x0002;

/// Bit 15 set: an event id, not a method id.
inline constexpr std::uint16_t kOnSpeedChanged = 0x8001;

/// Where requests are sent. Unicast: a request has exactly one recipient.
inline constexpr std::uint16_t kMethodPort = 30509;

/// Where events are published. In a full stack these would be *learned* from
/// Service Discovery rather than compiled in -- which is week 9.
inline constexpr const char* kEventGroup = "239.10.0.2";
inline constexpr std::uint16_t kEventPort = 30510;

}  // namespace av::service::vehicle
