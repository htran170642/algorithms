#include "av/qt/loop_monitor.hpp"

#include <QObject>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace av::qt {
namespace {

/// Sort, index by fraction, clamp. The same convention as apps/ipc_bench so the
/// two places agree on what a p99 is.
std::chrono::milliseconds at(const std::vector<std::chrono::milliseconds>& sorted,
                            double fraction) {
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size()));
    return sorted[std::min(index, sorted.size() - 1U)];
}

}  // namespace

LoopMonitor::LoopMonitor(std::chrono::milliseconds interval, QObject* parent)
    : QObject(parent), interval_(interval) {
    // The default, Qt::CoarseTimer, may drift by 5% of the interval so the
    // kernel can group wakeups. That is the right default for a UI and the
    // wrong one for a ruler.
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(static_cast<int>(interval_.count()));

    // Receiver is `this`, and `this` lives on the thread that constructed it,
    // so Qt::AutoConnection resolves to direct: on_tick runs *on the loop being
    // measured*. Measuring from another thread would measure the wrong loop.
    connect(&timer_, &QTimer::timeout, this, &LoopMonitor::on_tick);
}

void LoopMonitor::start() {
    last_ = Clock::now();
    timer_.start();
}

void LoopMonitor::stop() { timer_.stop(); }

void LoopMonitor::reset() {
    gaps_.clear();
    last_ = Clock::now();
}

void LoopMonitor::on_tick() {
    const auto now = Clock::now();
    const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_);
    last_ = now;
    gaps_.push_back(gap);

    // A timer that could not fire does not fire N times afterwards to catch up:
    // Qt drops what it missed. So blocking the loop for 3 s shows up as one
    // enormous gap, never as a burst of small ones -- which is why counting
    // ticks is a bad clock, and why `worst` is the number that matters.
    const auto late = gap - interval_;
    if (late > interval_) {
        emit stalled(static_cast<int>(late.count()));
    }
}

LoopLatency LoopMonitor::latency() const {
    if (gaps_.empty()) {
        return {};
    }
    auto sorted = gaps_;
    std::sort(sorted.begin(), sorted.end());
    return LoopLatency{gaps_.size(), at(sorted, 0.50), at(sorted, 0.99), sorted.back()};
}

}  // namespace av::qt
