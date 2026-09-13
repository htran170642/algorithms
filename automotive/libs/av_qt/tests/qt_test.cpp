// Week 11. The event loop, measured -- and the four things about signals and
// slots that decide whether a cockpit UI stutters.
//
// The shape is week 10's mw_test: one thread, a loop pumped by hand. Qt's
// QCoreApplication::exec() is the `while (!stop) runtime->poll()` loop of
// svc_client, so these tests run it in bounded slices (pump, processEvents,
// QSignalSpy::wait) rather than calling exec() and never coming back.
//
// Timing assertions are deliberately loose. They assert orders of magnitude --
// 10 ms versus 300 ms -- not real-time guarantees. A Linux desktop makes no
// such guarantee; that argument belongs to week 16's QNX.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

#include "av/qt/loop_monitor.hpp"
#include "signal_probe.hpp"

namespace {

// `avqt`, not `qt`: Qt's own global namespace is `Qt`, and two names one
// shift-key apart is how a reader loses an hour.
namespace avqt = av::qt;

using avqt::test::Receiver;
using avqt::test::Sender;
using avqt::test::Worker;
using namespace std::chrono_literals;

/// The monitor's interval. Short enough that a few hundred ms of running
/// collects plenty of samples, long enough not to be its own load.
constexpr auto kTick = std::chrono::milliseconds{10};

/// Runs the event loop for `duration`, then returns -- which exec() never does.
/// This is the test's poll().
void pump(std::chrono::milliseconds duration) {
    QEventLoop loop;
    QTimer::singleShot(static_cast<int>(duration.count()), &loop, &QEventLoop::quit);
    loop.exec();
}

/// Breaks the one rule: occupies the thread without returning to the loop.
/// A sleep and a busy CPU loop are indistinguishable from the loop's side --
/// all it knows is that it did not get control back.
void block(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }

class EventLoop : public ::testing::Test {
protected:
    EventLoop() : app_(argc_, argv_) {}

    int argc_{1};
    // QCoreApplication wants main()'s argc/argv. There is no std::array
    // overload, so these stay C arrays.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    char argv0_[16]{"av_qt_test"};
    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    char* argv_[2]{argv0_, nullptr};
    QCoreApplication app_;
};

// ---------------------------------------------------------------------------
// The event loop, as a measurable thing
// ---------------------------------------------------------------------------

TEST_F(EventLoop, AnIdleLoopComesBackOnTime) {
    avqt::LoopMonitor monitor{kTick};
    monitor.start();

    pump(300ms);

    const auto latency = monitor.latency();
    EXPECT_GT(latency.ticks, 10U);
    EXPECT_LT(latency.worst.count(), 100);
}

TEST_F(EventLoop, ABlockingSlotShowsUpAsOneEnormousGap) {
    avqt::LoopMonitor monitor{kTick};
    monitor.start();

    // The rule broken, in one line: a slot that does not return.
    QTimer::singleShot(50, &app_, [] { block(300ms); });
    pump(500ms);

    const auto latency = monitor.latency();
    EXPECT_GE(latency.worst.count(), 300);

    // 300 ms of blocking did not queue up 30 missed ticks to replay afterwards:
    // Qt drops what a timer could not deliver. So ~450 ms of loop time produced
    // far fewer than 45 samples -- which is why counting ticks is a bad clock.
    EXPECT_LT(latency.ticks, 35U);

    // And the median is untouched. This is the whole reason the demo reports a
    // worst case: an average would have hidden a three-second freeze.
    EXPECT_LT(latency.p50.count(), 50);
}

TEST_F(EventLoop, AStalledLoopReportsOnceNotPerMissedTick) {
    avqt::LoopMonitor monitor{kTick};
    QSignalSpy stalls(&monitor, &avqt::LoopMonitor::stalled);
    monitor.start();

    QTimer::singleShot(50, &app_, [] { block(200ms); });
    pump(400ms);

    // One report for one stall. Not the ~20 ticks the block consumed -- there
    // is no backlog to drain, because Qt never queued them.
    EXPECT_GE(stalls.count(), 1);
    EXPECT_LT(stalls.count(), 5);
    EXPECT_GE(stalls.at(0).at(0).toInt(), 150);
}

TEST_F(EventLoop, TheSameWorkInSlicesKeepsTheLoopResponsive) {
    avqt::LoopMonitor monitor{kTick};
    monitor.start();

    // The same 300 ms of work as ABlockingSlotShowsUpAsOneEnormousGap, handed
    // back to the loop after every 5 ms slice. The work still finishes; the
    // loop never stops. This is precisely what av_mw asks of a callback, and
    // what week 13's animations will need.
    int slices_left = 60;
    std::function<void()> slice;
    slice = [&] {
        block(5ms);
        if (--slices_left > 0) {
            QTimer::singleShot(0, &app_, slice);
        }
    };
    QTimer::singleShot(50, &app_, slice);

    pump(600ms);

    EXPECT_EQ(slices_left, 0) << "the work has to actually finish for this to mean anything";
    // Compare against the >= 300 above. Same work, same thread, no extra core.
    EXPECT_LT(monitor.latency().worst.count(), 100);
}

// ---------------------------------------------------------------------------
// Signals and slots: when does the slot actually run?
// ---------------------------------------------------------------------------

TEST_F(EventLoop, ADirectConnectionRunsTheSlotBeforeEmitReturns) {
    Sender sender;
    Receiver receiver;
    QObject::connect(&sender, &Sender::pinged, &receiver, &Receiver::on_pinged,
                     Qt::DirectConnection);

    sender.fire(7);

    // No loop was run, and the slot already ran. A direct connection is a
    // function call wearing a signal's clothes -- which is also exactly why a
    // slow slot blocks whoever emitted.
    EXPECT_EQ(receiver.calls, 1);
    EXPECT_EQ(receiver.last, 7);
}

TEST_F(EventLoop, AQueuedConnectionWaitsForTheNextTripThroughTheLoop) {
    Sender sender;
    Receiver receiver;
    QObject::connect(&sender, &Sender::pinged, &receiver, &Receiver::on_pinged,
                     Qt::QueuedConnection);

    sender.fire(7);
    EXPECT_EQ(receiver.calls, 0) << "queued means later, and later has not happened yet";

    QCoreApplication::processEvents();
    EXPECT_EQ(receiver.calls, 1);
    EXPECT_EQ(receiver.last, 7);

    // This is av_mw::Runtime::post(), spelled in Qt. Week 10 needed it for the
    // same reason: a completion that fires inside the call that started it
    // clears `waiting` before the caller has set it.
}

// ---------------------------------------------------------------------------
// Lifetime: Qt's answer to week 10's weak_ptr
// ---------------------------------------------------------------------------

TEST_F(EventLoop, ADeletedReceiverDisconnectsItselfAndLeavesTheRestAlone) {
    Sender sender;
    Receiver survivor;
    auto doomed = std::make_unique<Receiver>();

    QObject::connect(&sender, &Sender::pinged, doomed.get(), &Receiver::on_pinged);
    QObject::connect(&sender, &Sender::pinged, &survivor, &Receiver::on_pinged);

    doomed.reset();
    sender.fire(3);

    // The dead one was dropped, and only it. Nobody called disconnect(): a
    // QObject unregisters itself from every connection in its destructor. That
    // is what week 10 spent a shared_ptr/weak_ptr pair to achieve by hand.
    EXPECT_EQ(survivor.calls, 1);
    EXPECT_EQ(survivor.last, 3);
}

TEST_F(EventLoop, ADeletedContextObjectSilencesItsLambda) {
    Sender sender;
    bool ran = false;
    auto context = std::make_unique<QObject>();
    QObject::connect(&sender, &Sender::pinged, context.get(), [&ran](int) { ran = true; });

    context.reset();
    sender.fire(1);

    // A lambda has no destructor Qt can hook, so the *context object* decides
    // when the connection dies. Omit it -- connect(&sender, &sig, [&]{...}) --
    // and nothing ever disconnects: the lambda outlives what it captured and
    // reads freed memory. That overload compiles, which is the trap.
    EXPECT_FALSE(ran) << "Qt disconnected the lambda when its context died";
}

// ---------------------------------------------------------------------------
// Threads: which loop runs the slot
// ---------------------------------------------------------------------------

TEST_F(EventLoop, AnObjectBelongsToTheThreadThatCreatedIt) {
    QThread worker;
    auto receiver = std::make_unique<Receiver>();
    EXPECT_EQ(receiver->thread(), QThread::currentThread());

    receiver->moveToThread(&worker);
    EXPECT_EQ(receiver->thread(), &worker);

    // There is deliberately no moveToThread back to here, because Qt would
    // refuse it: an object may be *pushed* to another thread by the thread it
    // currently lives on, and cannot be *pulled* back from outside. Try it and
    // Qt prints "Cannot move to target thread" and changes nothing -- affinity
    // is a one-way street. Which is why the pattern is: construct, move once,
    // then leave the object alone.
    //
    // `worker` never started, so no loop holds events for this object and
    // destroying it from here is safe. When the thread *has* run, Qt's own
    // answer is to connect QThread::finished to deleteLater rather than to
    // delete across the boundary.
    EXPECT_FALSE(worker.isRunning());
    receiver.reset();
}

TEST_F(EventLoop, AWorkerThreadsSignalArrivesOnTheReceiversThread) {
    const QThread* const main_thread = QThread::currentThread();

    // Declaration order is destruction order reversed: `thread` must outlive
    // the object whose affinity points at it. Week 10's ownership row, again.
    QThread thread;
    Worker worker;
    Receiver receiver;  // stays here, on the main thread

    QObject::connect(&worker, &Worker::done, &receiver, &Receiver::on_pinged);
    worker.moveToThread(&thread);
    QObject::connect(&thread, &QThread::started, &worker, &Worker::run);

    QSignalSpy done(&worker, &Worker::done);
    thread.start();
    ASSERT_TRUE(done.wait(2000));

    thread.quit();
    ASSERT_TRUE(thread.wait(2000));

    EXPECT_EQ(worker.ran_on, &thread) << "the work happened off the main thread";
    EXPECT_EQ(receiver.seen_on, main_thread) << "the answer came back on the main thread";
    EXPECT_EQ(receiver.last, 42);

    // Not one mutex in this test. Qt::AutoConnection saw that sender and
    // receiver live on different threads and posted an event instead of
    // calling -- the same move as av_mw::post() and week 2's BoundedQueue.
    // The queued connection *is* the synchronisation.
}

}  // namespace
