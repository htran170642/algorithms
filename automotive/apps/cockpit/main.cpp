// The cockpit's thread model, in miniature.
//
//   [rx]  --frames-->  [decode]  --state-->  [ui]
//    |                    |                    |
//  never blocks       may block on rx     deliberately slow
//
// Three threads, two bounded queues, one shutdown that must not hang. The
// interesting part is not that it works -- it is what each thread does when
// the thread downstream cannot keep up, and how the whole thing stops.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "av/can/bit.hpp"
#include "av/can/frame.hpp"
#include "av/can/signal.hpp"
#include "av/conc/bounded_queue.hpp"
#include "av/log.hpp"

namespace {

using av::can::ByteOrder;
using av::can::CanFrame;
using av::can::SignalSpec;
using av::conc::BoundedQueue;
using av::conc::PushStatus;

namespace log = av::log;

constexpr std::uint32_t kEngineDataId = 0x100U;
constexpr int kFramesToSend = 200;

// Same message as week 1's spine demo.
constexpr SignalSpec kSpeed{"VehicleSpeed", 0U, 16U, ByteOrder::Intel, false, 0.01, 0.0, "km/h"};
constexpr SignalSpec kRpm{"EngineRpm", 23U, 16U, ByteOrder::Motorola, false, 0.25, 0.0, "rpm"};
constexpr SignalSpec kTemp{"CoolantTemp", 32U, 8U, ByteOrder::Intel, false, 1.0, -40.0, "degC"};

/// What the decoder hands the UI. Default-constructible because BoundedQueue
/// pre-allocates its slots.
struct VehicleState {
    std::uint64_t sequence{};
    double speed{};
    double rpm{};
    double temperature{};
};

/// Counters the shutdown report is built from. Atomic because three threads
/// touch them; relaxed ordering would be enough, but these are not on the hot
/// path and the default is one less thing to get wrong.
struct Stats {
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> frames_dropped{0};
    std::atomic<std::uint64_t> decoded{0};
    std::atomic<std::uint64_t> decode_failures{0};
    std::atomic<std::uint64_t> states_overwritten{0};
    std::atomic<std::uint64_t> displayed{0};
};

void say(const std::string& line) { std::cout << line << '\n' << std::flush; }

void heading(const char* text) { std::cout << '\n' << text << '\n' << std::flush; }

/// A triangle wave, so the speed on screen visibly moves.
double simulated_speed(int tick) {
    constexpr int kPeriod = 100;
    const int phase = tick % kPeriod;
    const int ramp = (phase < kPeriod / 2) ? phase : (kPeriod - phase);
    return static_cast<double>(ramp) * 2.0;  // 0 .. 100 km/h
}

// --- rx --------------------------------------------------------------------

/// Stands in for a SocketCAN read loop (week 5 replaces the body, not the shape).
///
/// Uses try_push and never blocks. A receive thread that stalls does not save
/// anything: the kernel's socket buffer fills behind it and the frames are lost
/// anyway, only now you cannot see it happen. Dropping here is visible.
void run_rx(BoundedQueue<CanFrame>& frames, Stats& stats) {
    for (int tick = 0; tick < kFramesToSend; ++tick) {
        CanFrame frame{};
        frame.id = kEngineDataId;
        frame.length = 8U;
        const double speed = simulated_speed(tick);
        static_cast<void>(av::can::encode(kSpeed, speed, frame));
        static_cast<void>(av::can::encode(kRpm, 800.0 + (speed * 20.0), frame));
        static_cast<void>(av::can::encode(kTemp, 90.0, frame));

        if (frames.try_push(frame) == PushStatus::Ok) {
            stats.produced.fetch_add(1);
        } else {
            stats.frames_dropped.fetch_add(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});  // ~1 kHz bus
    }
    log::info("rx", "receive loop finished", "sent", stats.produced.load(), "dropped",
              stats.frames_dropped.load());
}

// --- decode ----------------------------------------------------------------

/// Blocking pop upstream, non-blocking push downstream.
///
/// The asymmetry is the point. Upstream it may block: there is nothing to do
/// without a frame, and the queue has already absorbed the jitter. Downstream
/// it must not, because the UI is allowed to be slow and the decoder must not
/// inherit that.
///
/// The policy when the UI queue is full is *drop oldest*: vehicle state is a
/// latest-value signal, so a stale reading is worth less than a fresh one. An
/// event stream (a warning, a door-open transition) would need drop-newest, or
/// no dropping at all.
void run_decode(BoundedQueue<CanFrame>& frames, BoundedQueue<VehicleState>& states, Stats& stats) {
    std::uint64_t sequence = 0;

    while (const auto frame = frames.pop()) {
        const auto speed = av::can::decode(kSpeed, *frame);
        const auto rpm = av::can::decode(kRpm, *frame);
        const auto temperature = av::can::decode(kTemp, *frame);

        if (!speed || !rpm || !temperature) {
            // Week 1's rule, still holding: a missing signal is not a zero.
            stats.decode_failures.fetch_add(1);
            log::warn("decode", "incomplete frame", "id", frame->id, "len",
                      unsigned{frame->length});
            continue;
        }

        ++sequence;
        const VehicleState state{sequence, *speed, *rpm, *temperature};
        stats.decoded.fetch_add(1);

        if (states.try_push(state) == PushStatus::Full) {
            static_cast<void>(states.try_pop());  // make room by discarding the stalest
            static_cast<void>(states.try_push(state));
            stats.states_overwritten.fetch_add(1);
        }
    }

    log::info("decode", "input closed and drained", "decoded", stats.decoded.load());
}

// --- ui --------------------------------------------------------------------

/// Deliberately ten times slower than rx, so backpressure actually happens.
/// In week 11 this becomes the Qt event loop, and the same rule applies: the
/// thread that draws must never wait on the thread that receives.
void run_ui(BoundedQueue<VehicleState>& states, Stats& stats) {
    while (const auto state = states.pop()) {
        stats.displayed.fetch_add(1);
        if (state->sequence % 20U == 0U) {
            std::ostringstream os;
            os << "  seq " << std::setw(4) << state->sequence << " | " << std::fixed
               << std::setprecision(1) << std::setw(6) << state->speed << " km/h | "
               << std::setw(7) << state->rpm << " rpm | " << std::setw(5) << state->temperature
               << " degC";
            say(os.str());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    log::info("ui", "input closed and drained", "displayed", stats.displayed.load());
}

/// The whole application, so that main() itself can stay a try/catch and
/// nothing escapes it. Letting an exception leave main is std::terminate with
/// no message -- the one failure mode a cockpit process must never have.
int run() {
    log::set_level(log::Level::Info);

    // Small on purpose: a 2048-slot queue would hide the backpressure this
    // demo exists to show.
    BoundedQueue<CanFrame> frames{32};
    BoundedQueue<VehicleState> states{4};
    Stats stats;

    heading("Cockpit thread model -- rx (1 kHz) -> decode -> ui (100 Hz)");

    std::thread ui(run_ui, std::ref(states), std::ref(stats));
    std::thread decode(run_decode, std::ref(frames), std::ref(states), std::ref(stats));
    std::thread rx(run_rx, std::ref(frames), std::ref(stats));

    // Shutdown, strictly downstream-last. Closing `states` before the decoder
    // has drained `frames` would throw away work that was already accepted.
    rx.join();
    frames.close();

    decode.join();
    states.close();

    ui.join();

    heading("Shutdown report");
    std::ostringstream os;
    os << "  produced            " << stats.produced.load() << '\n'
       << "  frames dropped      " << stats.frames_dropped.load() << "   (rx queue full)\n"
       << "  decoded             " << stats.decoded.load() << '\n'
       << "  decode failures     " << stats.decode_failures.load() << '\n'
       << "  states overwritten  " << stats.states_overwritten.load() << "   (ui queue full)\n"
       << "  displayed           " << stats.displayed.load();
    say(os.str());

    // The invariant worth asserting out loud: nothing the decoder accepted was
    // silently lost. Every decoded state was either shown or explicitly
    // overwritten, and both are counted.
    const std::uint64_t accounted = stats.displayed.load() + stats.states_overwritten.load();
    if (accounted != stats.decoded.load()) {
        log::error("main", "state accounting does not balance", "decoded", stats.decoded.load(),
                   "displayed+overwritten", accounted);
        return 1;
    }

    say("\n  every decoded state was either displayed or explicitly dropped -- none lost silently");
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        log::error("main", "unhandled exception", "what", error.what());
        return 1;
    }
}
