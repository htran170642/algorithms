#pragma once

// A vehicle signal: the rule that turns a run of bits into a number a human
// would recognise, and back.
//
// Everything above this line in the cockpit stack -- the data model, the
// middleware, the QML binding -- deals in physical values. Everything below
// deals in bytes. This is the boundary.

#include <optional>
#include <string_view>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"

namespace av::can {

/// One `SG_` row of a DBC file.
///
///     SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h"
///         ^name          ^start        ^offset       ^unit
///                          ^length  ^factor
///                             ^byte order (1=Intel, 0=Motorola)
///                              ^sign (+ unsigned, - signed)
///
/// The `[min|max]` range is deliberately absent: range validation arrives in
/// week 6 together with the DBC parser, and a field that exists but is never
/// enforced is worse than no field at all.
struct SignalSpec {
    std::string_view name;
    unsigned start_bit{};
    unsigned length{};
    ByteOrder order{ByteOrder::Intel};
    bool is_signed{false};
    double factor{1.0};
    double offset{0.0};
    std::string_view unit;
};

/// physical = raw * factor + offset
///
/// Returns nullopt when the signal does not fit the frame -- a truncated or
/// malformed frame, or a spec that does not belong to this message.
std::optional<double> decode(const SignalSpec& spec, const CanFrame& frame) noexcept;

/// The inverse: raw = round((physical - offset) / factor)
///
/// Returns false, leaving `frame` untouched, when the value cannot be
/// represented in `spec.length` bits. This is the encoder's half of "out of
/// range" -- the transmitter refuses rather than silently wrapping.
bool encode(const SignalSpec& spec, double physical, CanFrame& frame) noexcept;

}  // namespace av::can
