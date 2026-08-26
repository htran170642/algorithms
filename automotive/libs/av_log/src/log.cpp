#include "av/log.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace av::log {
namespace {

std::atomic<Level>& level_storage() noexcept {
    static std::atomic<Level> value{Level::Info};
    return value;
}

std::mutex& sink_mutex() noexcept {
    static std::mutex mutex;
    return mutex;
}

/// Guarded by sink_mutex().
Sink& sink_storage() noexcept {
    static Sink sink;
    return sink;
}

std::string timestamp() {
    using clock = std::chrono::system_clock;

    const clock::time_point now = clock::now();
    const clock::time_point whole = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - whole).count();

    const std::time_t seconds = clock::to_time_t(whole);
    std::tm broken_down{};
    ::localtime_r(&seconds, &broken_down);

    std::array<char, 24> date{};
    const std::size_t length =
        std::strftime(date.data(), date.size(), "%Y-%m-%dT%H:%M:%S", &broken_down);

    std::array<char, 8> fraction{};
    if (std::snprintf(fraction.data(), fraction.size(), ".%03d", static_cast<int>(millis)) < 0) {
        fraction[0] = '\0';  // Encoding error: emit the line without milliseconds.
    }

    return std::string(date.data(), length) + fraction.data();
}

}  // namespace

std::string_view to_string(Level value) noexcept {
    switch (value) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO ";
        case Level::Warn:
            return "WARN ";
        case Level::Error:
            return "ERROR";
    }
    return "?????";
}

void set_level(Level value) noexcept {
    level_storage().store(value, std::memory_order_relaxed);
}

Level level() noexcept {
    return level_storage().load(std::memory_order_relaxed);
}

void set_sink(Sink sink) {
    const std::lock_guard<std::mutex> lock(sink_mutex());
    sink_storage() = std::move(sink);
}

namespace detail {

void emit(Level value, std::string_view component, std::string_view message,
          std::string_view fields) {
    // Build the whole line before taking the lock, so formatting cost is not
    // serialised across threads -- only the handoff to the sink is.
    std::string line;
    line.reserve(32 + component.size() + message.size() + fields.size());
    line += timestamp();
    line += ' ';
    line += to_string(value);
    line += ' ';
    line += component;
    line += "  ";
    line += message;
    line += fields;

    const std::lock_guard<std::mutex> lock(sink_mutex());
    if (sink_storage()) {
        sink_storage()(line);
    } else {
        // One fputs per line: two threads must never split a line between them.
        line += '\n';
        if (std::fputs(line.c_str(), stderr) == EOF) {
            // Deliberately swallowed: the logger is the last line of defence,
            // so it must never throw or recurse into itself on failure.
        }
    }
}

}  // namespace detail
}  // namespace av::log
