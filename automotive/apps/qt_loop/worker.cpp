#include "worker.hpp"

#include <QObject>

#include <chrono>
#include <thread>

namespace qt_loop {
namespace {

/// How often the worker looks up from its work -- to report, and to notice that
/// it has been cancelled. Cooperative cancellation, exactly as in week 2: the
/// only thread that can stop this work is the one doing it.
constexpr int kSteps = 30;

}  // namespace

void Worker::run(int total_ms) {
    cancelled_.store(false);

    const auto slice = std::chrono::milliseconds{total_ms / kSteps};
    for (int step = 1; step <= kSteps; ++step) {
        if (cancelled_.load()) {
            break;
        }
        std::this_thread::sleep_for(slice);

        // Sender and receiver live on different threads, so Qt::AutoConnection
        // resolves this to a posted event. The decision is made *here*, at emit
        // time, from the two objects' current affinities -- not back when
        // connect() was called. Which is why Window can wire everything up
        // before moving this object, and still get queued delivery.
        emit progressed(step * 100 / kSteps);
    }

    emit finished();
}

}  // namespace qt_loop
