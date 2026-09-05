#pragma once

// A DBC database: the file that says what a CAN identifier *means*.
//
// Week 1 built one SignalSpec by hand. Week 5 shared a hardcoded table between
// two programs. Both were stand-ins for this: in a real project the signal
// definitions arrive as a `.dbc` file, written by whoever owns the bus, and
// every ECU on the network decodes from the same document.
//
// That is the whole point. A CAN frame carries no self-description -- eight
// bytes and a number. The DBC is the *only* thing that turns `CC 1C` into
// "73.72 km/h", and if the transmitter and the receiver hold different copies
// of it, nothing anywhere reports a problem. The number is simply wrong.
//
// Format, by example:
//
//     BO_ 256 EngineData: 8 ECU_Powertrain
//      SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster
//         ^name          ^start     ^factor  ^min      ^unit  ^receiver
//                          ^length     ^offset  ^max
//                             ^byte order (1=Intel, 0=Motorola)
//                              ^sign (+ unsigned, - signed)
//
// Scope: BO_, SG_, CM_ and VAL_ are parsed. NS_, BS_, BU_, BA_* and the rest
// are skipped, because nothing in this project reads them yet and a parser
// that silently half-understands an attribute is worse than one that ignores
// it visibly.

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"
#include "av/can/signal.hpp"

namespace av::can {

/// One `SG_` row, with the parts week 1 deliberately left out.
///
/// A plain struct rather than a class with accessors, matching SignalSpec and
/// CanFrame: it is data, and it has no invariant to protect beyond what the
/// parser already checked.
struct DbcSignal {
    std::string name;
    unsigned start_bit{};
    unsigned length{};
    ByteOrder order{ByteOrder::Intel};
    bool is_signed{false};
    double factor{1.0};
    double offset{0.0};

    /// The `[min|max]` pair. Week 1's SignalSpec omitted it on purpose --
    /// "a field that exists but is never enforced is worse than no field at
    /// all". Here it is enforced, by in_range() and by encode().
    double minimum{0.0};
    double maximum{0.0};
    bool has_range{false};

    std::string unit;
    std::string comment;

    /// The `VAL_` table: raw value -> human name, for enumerations such as the
    /// door and warning bitfields. Empty for ordinary numeric signals.
    std::map<std::int64_t, std::string> values;

    /// A week-1 SignalSpec for this signal.
    ///
    /// The returned spec's `name` and `unit` are string_views **borrowed from
    /// this object**. It must not outlive the DbcSignal, and the DbcSignal
    /// must not be moved while a spec taken from it is still in use. Same
    /// contract as std::string::c_str().
    [[nodiscard]] SignalSpec spec() const noexcept;

    /// Physical value, or nullopt when the signal does not fit the frame.
    ///
    /// Deliberately does *not* apply the range check: a value outside
    /// [min|max] is a fact about the bus that the caller must be able to see
    /// and log. Silently dropping it would hide the fault.
    [[nodiscard]] std::optional<double> decode(const CanFrame& frame) const noexcept;

    /// Refuses out-of-range values as well as unrepresentable ones. A
    /// transmitter that cannot honour the DBC must not send at all.
    [[nodiscard]] bool encode(double physical, CanFrame& frame) const noexcept;

    [[nodiscard]] bool in_range(double physical) const noexcept;

    /// The `VAL_` name for a decoded value, or empty if there is none.
    [[nodiscard]] std::string_view value_name(double physical) const noexcept;
};

/// One `BO_` row and the signals under it.
struct DbcMessage {
    std::uint32_t id{};
    bool extended{false};
    std::string name;
    std::uint8_t length{};
    std::string transmitter;
    std::string comment;
    std::vector<DbcSignal> signals;

    [[nodiscard]] const DbcSignal* find(std::string_view signal) const noexcept;
};

class DbcDatabase {
public:
    /// Parses DBC text. Returns nullopt on a malformed BO_ or SG_ line, having
    /// logged the line number -- a parser that cannot say *where* it failed is
    /// not usable on a 4000-line production file.
    static std::optional<DbcDatabase> parse(std::istream& input);

    static std::optional<DbcDatabase> load(const std::string& path);

    /// Looks up a message. `extended` matters: a standard 0x100 and an
    /// extended 0x100 are different messages and may carry different signals.
    [[nodiscard]] const DbcMessage* find(std::uint32_t id, bool extended = false) const noexcept;

    [[nodiscard]] const std::vector<DbcMessage>& messages() const noexcept { return messages_; }
    [[nodiscard]] const std::string& version() const noexcept { return version_; }

private:
    std::vector<DbcMessage> messages_;
    std::string version_;
};

}  // namespace av::can
