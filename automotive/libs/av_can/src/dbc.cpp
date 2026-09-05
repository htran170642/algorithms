#include "av/can/dbc.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <istream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"
#include "av/can/signal.hpp"
#include "av/log.hpp"

namespace av::can {
namespace {

namespace log = av::log;

/// DBC marks an extended identifier by setting bit 31 of the BO_ number.
///
/// This is the classic DBC trap. `BO_ 2147483939` is not a nonsense id; it is
/// extended id 0x123 with the flag on (0x80000000 | 0x123). A parser that
/// reads it literally produces a message nothing on the bus will ever match.
constexpr std::uint32_t kExtendedFlag = 0x8000'0000U;

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/// Reads a `"..."` literal at the current position. Returns false if the next
/// non-space character is not a quote.
bool read_quoted(std::istream& in, std::string& out) {
    char quote = 0;
    if (!(in >> quote) || quote != '"') {
        return false;
    }
    std::getline(in, out, '"');
    return !in.fail();
}

/// `BO_ 256 EngineData: 8 ECU_Powertrain`
bool parse_message(const std::string& line, DbcMessage& out) {
    std::istringstream in(line);
    std::string keyword;
    std::uint32_t raw_id = 0;
    std::string name;

    if (!(in >> keyword >> raw_id >> name)) {
        return false;
    }
    // The name is written `EngineData:` -- the colon is glued to it.
    if (name.empty() || name.back() != ':') {
        return false;
    }
    name.pop_back();

    unsigned length = 0;
    if (!(in >> length) || length > kMaxPayload) {
        return false;
    }
    std::string transmitter;
    in >> transmitter;  // optional; a message with no sender is legal

    out = DbcMessage{};
    out.extended = (raw_id & kExtendedFlag) != 0U;
    out.id = out.extended ? (raw_id & kExtendedIdMask) : raw_id;
    if (!out.extended && out.id > kStandardIdMask) {
        return false;  // an 11-bit id that does not fit in 11 bits
    }
    out.name = std::move(name);
    out.length = static_cast<std::uint8_t>(length);
    out.transmitter = std::move(transmitter);
    return true;
}

/// `SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|655.35] "km/h" Cluster`
///
/// `multiplexed` is set when the row carries an `M` or `m<n>` token, which
/// this parser does not support. The caller rejects the file rather than
/// loading half of it.
bool parse_signal(const std::string& line, DbcSignal& out, bool& multiplexed) {
    multiplexed = false;

    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }

    // --- before the colon: "SG_ Name [mux]" ---
    std::istringstream head(line.substr(0, colon));
    std::string keyword;
    std::string name;
    if (!(head >> keyword >> name)) {
        return false;
    }
    std::string mux;
    if (head >> mux && !mux.empty()) {
        multiplexed = true;
        return false;
    }

    // --- after the colon: the layout ---
    std::istringstream in(line.substr(colon + 1));

    unsigned start = 0;
    unsigned length = 0;
    char bar = 0;
    char at = 0;
    char order = 0;
    char sign = 0;
    if (!(in >> start >> bar >> length >> at >> order >> sign) || bar != '|' || at != '@') {
        return false;
    }
    if ((order != '0' && order != '1') || (sign != '+' && sign != '-')) {
        return false;
    }

    char open = 0;
    char comma = 0;
    char close = 0;
    double factor = 1.0;
    double offset = 0.0;
    if (!(in >> open >> factor >> comma >> offset >> close) || open != '(' || comma != ',' ||
        close != ')') {
        return false;
    }

    char lbracket = 0;
    char bar2 = 0;
    char rbracket = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    if (!(in >> lbracket >> minimum >> bar2 >> maximum >> rbracket) || lbracket != '[' ||
        bar2 != '|' || rbracket != ']') {
        return false;
    }

    std::string unit;
    if (!read_quoted(in, unit)) {
        return false;
    }
    // The receiver list is the rest of the line; nothing here consumes it.

    out = DbcSignal{};
    out.name = std::move(name);
    out.start_bit = start;
    out.length = length;
    out.order = (order == '1') ? ByteOrder::Intel : ByteOrder::Motorola;
    out.is_signed = (sign == '-');
    out.factor = factor;
    out.offset = offset;
    out.minimum = minimum;
    out.maximum = maximum;
    // Many tools emit [0|0] to mean "no range stated". Deciding that once,
    // here, keeps a float comparison out of in_range().
    out.has_range = minimum != 0.0 || maximum != 0.0;
    out.unit = std::move(unit);
    return true;
}

/// `CM_ BO_ 256 "text";` and `CM_ SG_ 256 VehicleSpeed "text";`
///
/// Multi-line comments exist in the wild and are not handled: a comment whose
/// closing quote is on a later line is skipped rather than mis-attached.
void apply_comment(const std::string& line, std::vector<DbcMessage>& messages) {
    std::istringstream in(line);
    std::string keyword;
    std::string kind;
    in >> keyword >> kind;

    if (kind != "BO_" && kind != "SG_") {
        return;  // BU_, EV_, or a global comment: nothing here reads them
    }

    std::uint32_t raw_id = 0;
    if (!(in >> raw_id)) {
        return;
    }
    const bool extended = (raw_id & kExtendedFlag) != 0U;
    const std::uint32_t id = extended ? (raw_id & kExtendedIdMask) : raw_id;

    std::string signal_name;
    if (kind == "SG_" && !(in >> signal_name)) {
        return;
    }

    std::string text;
    if (!read_quoted(in, text)) {
        return;
    }

    for (auto& message : messages) {
        if (message.id != id || message.extended != extended) {
            continue;
        }
        if (kind == "BO_") {
            message.comment = std::move(text);
            return;
        }
        for (auto& signal : message.signals) {
            if (signal.name == signal_name) {
                signal.comment = std::move(text);
                return;
            }
        }
        return;
    }
}

/// `VAL_ 512 DoorStatus 0 "AllClosed" 1 "DriverOpen" ... ;`
void apply_values(const std::string& line, std::vector<DbcMessage>& messages) {
    std::istringstream in(line);
    std::string keyword;
    std::uint32_t raw_id = 0;
    std::string signal_name;
    if (!(in >> keyword >> raw_id >> signal_name)) {
        return;
    }
    const bool extended = (raw_id & kExtendedFlag) != 0U;
    const std::uint32_t id = extended ? (raw_id & kExtendedIdMask) : raw_id;

    std::map<std::int64_t, std::string> table;
    while (true) {
        std::int64_t value = 0;
        if (!(in >> value)) {
            break;  // the trailing ';', or end of line
        }
        std::string label;
        if (!read_quoted(in, label)) {
            break;
        }
        table.emplace(value, std::move(label));
    }
    if (table.empty()) {
        return;
    }

    for (auto& message : messages) {
        if (message.id != id || message.extended != extended) {
            continue;
        }
        for (auto& signal : message.signals) {
            if (signal.name == signal_name) {
                signal.values = std::move(table);
                return;
            }
        }
        return;
    }
}

/// Parses a BO_ line and appends the message. False means the file is broken.
bool add_message(const std::string& line, unsigned line_number,
                 std::vector<DbcMessage>& messages) {
    DbcMessage message;
    if (!parse_message(line, message)) {
        log::error("dbc", "malformed BO_ line", "line", line_number, "text", line);
        return false;
    }
    messages.push_back(std::move(message));
    return true;
}

/// Parses an SG_ line and appends it to the message currently being built.
bool add_signal(const std::string& line, unsigned line_number, std::vector<DbcMessage>& messages) {
    if (messages.empty()) {
        log::error("dbc", "SG_ before any BO_", "line", line_number);
        return false;
    }
    DbcSignal signal;
    bool multiplexed = false;
    if (!parse_signal(line, signal, multiplexed)) {
        if (multiplexed) {
            log::error("dbc", "multiplexed signals are not supported", "line", line_number, "text",
                       line);
        } else {
            log::error("dbc", "malformed SG_ line", "line", line_number, "text", line);
        }
        return false;
    }
    messages.back().signals.push_back(std::move(signal));
    return true;
}

}  // namespace

// --- DbcSignal ------------------------------------------------------------

SignalSpec DbcSignal::spec() const noexcept {
    return SignalSpec{name, start_bit, length, order, is_signed, factor, offset, unit};
}

std::optional<double> DbcSignal::decode(const CanFrame& frame) const noexcept {
    return av::can::decode(spec(), frame);
}

bool DbcSignal::encode(double physical, CanFrame& frame) const noexcept {
    if (!in_range(physical)) {
        return false;
    }
    return av::can::encode(spec(), physical, frame);
}

bool DbcSignal::in_range(double physical) const noexcept {
    if (!has_range) {
        return true;
    }
    return physical >= minimum && physical <= maximum;
}

std::string_view DbcSignal::value_name(double physical) const noexcept {
    if (values.empty()) {
        return {};
    }
    const auto raw = static_cast<std::int64_t>(std::llround((physical - offset) / factor));
    const auto found = values.find(raw);
    if (found == values.end()) {
        return {};
    }
    return found->second;
}

// --- DbcMessage -----------------------------------------------------------

const DbcSignal* DbcMessage::find(std::string_view signal) const noexcept {
    for (const auto& candidate : signals) {
        if (candidate.name == signal) {
            return &candidate;
        }
    }
    return nullptr;
}

// --- DbcDatabase ----------------------------------------------------------

std::optional<DbcDatabase> DbcDatabase::parse(std::istream& input) {
    DbcDatabase database;
    std::string raw;
    unsigned line_number = 0;

    while (std::getline(input, raw)) {
        ++line_number;
        const std::string line = trim(raw);
        if (line.empty()) {
            continue;
        }

        if (starts_with(line, "VERSION")) {
            std::istringstream in(line);
            std::string keyword;
            in >> keyword;
            std::string version;
            if (read_quoted(in, version)) {
                database.version_ = std::move(version);
            }
            continue;
        }

        if (starts_with(line, "BO_ ")) {
            if (!add_message(line, line_number, database.messages_)) {
                return std::nullopt;
            }
            continue;
        }

        if (starts_with(line, "SG_ ")) {
            if (!add_signal(line, line_number, database.messages_)) {
                return std::nullopt;
            }
            continue;
        }

        if (starts_with(line, "CM_ ")) {
            apply_comment(line, database.messages_);
            continue;
        }
        if (starts_with(line, "VAL_ ")) {
            apply_values(line, database.messages_);
            continue;
        }
        // NS_, BS_, BU_, BA_*, EV_ and friends: skipped on purpose.
    }

    if (database.messages_.empty()) {
        log::error("dbc", "no BO_ messages found", "lines", line_number);
        return std::nullopt;
    }

    log::info("dbc", "database loaded", "version", database.version_, "messages",
              database.messages_.size());
    return database;
}

std::optional<DbcDatabase> DbcDatabase::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        log::error("dbc", "cannot open file", "path", path);
        return std::nullopt;
    }
    return parse(file);
}

const DbcMessage* DbcDatabase::find(std::uint32_t id, bool extended) const noexcept {
    for (const auto& message : messages_) {
        if (message.id == id && message.extended == extended) {
            return &message;
        }
    }
    return nullptr;
}

}  // namespace av::can
