#pragma once

/// Structured logging for the Cockpit DC lab.
///
/// CLAUDE.md section 8 requires structured logging, and section 7 requires every
/// fault to travel `Fault -> Detection -> Logging -> Fallback -> Safe State`.
/// That makes the logger a week-0 dependency, not a nicety: the fault-injection
/// work in weeks 14-15 has nothing to assert on without it.
///
/// Lines are `key=value`, not prose, so they stay greppable when the CAN
/// receiver, the service and the UI are all writing at once:
///
///     2026-08-26T10:34:12.123 INFO  can.rx  frame decoded id=291 dlc=8
///
/// Usage:
///     av::log::info("can.rx", "frame decoded", "id", 0x123, "dlc", 8);

#include <cstdint>
#include <functional>
#include <ostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace av::log {

// uint8_t, not int: this enum ends up inside log records and, later, inside
// diagnostic payloads where every byte is budgeted.
enum class Level : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

/// Five-character, space-padded name ("INFO ", "ERROR") so columns line up.
std::string_view to_string(Level value) noexcept;

/// Messages below this level are dropped. Default: Info.
void set_level(Level value) noexcept;
Level level() noexcept;

/// Receives one fully formatted line, without a trailing newline.
using Sink = std::function<void(std::string_view line)>;

/// Redirect formatted lines. A default-constructed Sink restores the built-in
/// stderr sink. Tests use this to capture output instead of scraping stderr.
void set_sink(Sink sink);

namespace detail {

void emit(Level value, std::string_view component, std::string_view message,
          std::string_view fields);

/// Recursion terminator for the variadic overload below.
inline void append_fields([[maybe_unused]] std::ostream& os) noexcept {}

template <typename Key, typename Value, typename... Rest>
void append_fields(std::ostream& os, const Key& key, const Value& value, Rest&&... rest) {
    os << ' ' << key << '=' << value;
    append_fields(os, std::forward<Rest>(rest)...);
}

template <typename... Fields>
void write(Level value, std::string_view component, std::string_view message,
           Fields&&... fields) {
    static_assert(sizeof...(Fields) % 2 == 0,
                  "log fields must be key/value pairs, e.g. \"id\", 0x123");

    // Cheap relaxed load first: a dropped Trace line must not cost a format.
    if (value < level()) {
        return;
    }

    std::ostringstream os;
    os << std::boolalpha;
    append_fields(os, std::forward<Fields>(fields)...);
    emit(value, component, message, os.str());
}

}  // namespace detail

template <typename... Fields>
void trace(std::string_view component, std::string_view message, Fields&&... fields) {
    detail::write(Level::Trace, component, message, std::forward<Fields>(fields)...);
}

template <typename... Fields>
void debug(std::string_view component, std::string_view message, Fields&&... fields) {
    detail::write(Level::Debug, component, message, std::forward<Fields>(fields)...);
}

template <typename... Fields>
void info(std::string_view component, std::string_view message, Fields&&... fields) {
    detail::write(Level::Info, component, message, std::forward<Fields>(fields)...);
}

template <typename... Fields>
void warn(std::string_view component, std::string_view message, Fields&&... fields) {
    detail::write(Level::Warn, component, message, std::forward<Fields>(fields)...);
}

template <typename... Fields>
void error(std::string_view component, std::string_view message, Fields&&... fields) {
    detail::write(Level::Error, component, message, std::forward<Fields>(fields)...);
}

}  // namespace av::log
