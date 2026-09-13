#pragma once

// Week 11. An instrument, so "the UI feels laggy" can become a number.
//
// Week 10 put one rule on every av_mw callback: do not block, because
// Runtime::poll() runs them on the caller's thread and a slow one stalls the
// whole process. Qt has the identical rule for slots, enforced by the identical
// mechanism -- one thread, one loop, one queue. This class measures the rule
// being broken.

#include <QObject>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <vector>

namespace av::qt {

using Clock = std::chrono::steady_clock;

/// How late the event loop ran, over the samples collected so far.
///
/// Percentiles follow the same convention as apps/ipc_bench (sort, index by
/// fraction, clamp to the last sample) so a p99 means one thing across the lab.
struct LoopLatency {
    std::size_t ticks{};
    std::chrono::milliseconds p50{};
    std::chrono::milliseconds p99{};
    std::chrono::milliseconds worst{};
};

/// Measures how long the event loop takes to come back.
///
/// A QTimer asked for 16 ms is a promise only the loop can keep. The gap
/// between two ticks is therefore not a property of the timer -- it is a
/// measurement of the slowest thing the loop did in between. That is the whole
/// instrument, and it is why the demo can say `worst 3004 ms` instead of
/// "it froze for a bit".
class LoopMonitor : public QObject {
    Q_OBJECT

public:
    explicit LoopMonitor(std::chrono::milliseconds interval, QObject* parent = nullptr);

    void start();
    void stop();
    /// Forget the samples but keep running -- "measure from here".
    void reset();

    [[nodiscard]] LoopLatency latency() const;
    [[nodiscard]] std::chrono::milliseconds interval() const { return interval_; }

signals:
    /// A tick arrived so late that at least one was missed entirely.
    ///
    /// `late_ms` is a plain int, not a std::chrono::milliseconds: a queued
    /// connection copies its arguments through Qt's metatype system, and Qt
    /// already knows how to copy an int. Teaching it a chrono type is possible
    /// (qRegisterMetaType) and is not this week's lesson.
    void stalled(int late_ms);

private:
    void on_tick();

    std::chrono::milliseconds interval_;
    QTimer timer_;
    Clock::time_point last_;  // time_point's default ctor already zeroes it
    std::vector<std::chrono::milliseconds> gaps_;
};

}  // namespace av::qt
