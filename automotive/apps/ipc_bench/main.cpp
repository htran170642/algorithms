// Unix domain socket versus shared memory, measured rather than argued about.
//
// Both carry the same payload between two real processes. The socket costs two
// copies and two syscalls per message; the shared ring costs one copy and no
// syscall at all once it is set up. The question this answers is not "which is
// faster" -- it is "by how much, and is that worth the synchronisation you now
// have to write yourself".
//
// Measured is round-trip time (ping-pong), because one-way timing across two
// processes needs a shared clock and RTT/2 is close enough for a decision.

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "av/ipc/shared_ring.hpp"
#include "av/ipc/unix_socket.hpp"
#include "av/log.hpp"

namespace {

using av::ipc::IoStatus;
using av::ipc::SharedRing;
using av::ipc::UnixSocket;

namespace log = av::log;
using Clock = std::chrono::steady_clock;

constexpr int kWarmup = 1000;
constexpr int kIterations = 20000;

/// Same shape as the VehicleState the cockpit passes between threads, so the
/// numbers below are about the transport and not about the payload.
struct Message {
    std::uint64_t sequence{};
    double speed{};
    double rpm{};
    double temperature{};
};

void say(const std::string& line) { std::cout << line << '\n' << std::flush; }

/// Percentiles matter more than the mean here: a cockpit misses a frame
/// because of the slow tail, never because of the average.
struct Summary {
    double median_ns{};
    double p99_ns{};
    double max_ns{};
};

Summary summarise(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&samples](double fraction) {
        const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size()));
        return samples[std::min(index, samples.size() - 1U)];
    };
    return Summary{at(0.50), at(0.99), samples.back()};
}

void report(const char* label, Summary summary) {
    std::ostringstream os;
    os << "  " << std::left << std::setw(22) << label << std::right << std::fixed
       << std::setprecision(2) << std::setw(10) << summary.median_ns / 1000.0 << " us"
       << std::setw(10) << summary.p99_ns / 1000.0 << " us" << std::setw(10)
       << summary.max_ns / 1000.0 << " us";
    say(os.str());
}

double to_ns(Clock::duration elapsed) {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

// --- Unix domain socket ----------------------------------------------------

/// Child side: echo every message straight back.
[[noreturn]] void socket_echo_child(const std::string& path) {
    auto peer = UnixSocket::connect(path);
    if (!peer) {
        ::_exit(1);
    }
    Message message{};
    for (int i = 0; i < kWarmup + kIterations; ++i) {
        if (peer->receive(&message, sizeof(message)).status != IoStatus::Ok) {
            ::_exit(2);
        }
        if (peer->send(&message, sizeof(message)) != IoStatus::Ok) {
            ::_exit(3);
        }
    }
    ::_exit(0);
}

std::vector<double> measure_socket(const std::string& path) {
    auto listener = UnixSocket::listen(path);
    if (!listener) {
        return {};
    }

    const pid_t child = ::fork();
    if (child == 0) {
        socket_echo_child(path);
    }

    auto peer = listener->accept();
    if (!peer) {
        return {};
    }

    std::vector<double> samples;
    samples.reserve(kIterations);
    Message message{};

    for (int i = 0; i < kWarmup + kIterations; ++i) {
        message.sequence = static_cast<std::uint64_t>(i);
        const auto start = Clock::now();

        if (peer->send(&message, sizeof(message)) != IoStatus::Ok) {
            break;
        }
        if (peer->receive(&message, sizeof(message)).status != IoStatus::Ok) {
            break;
        }

        // Warm-up iterations are discarded: the first few hundred include page
        // faults and branch predictor training that no steady state ever sees.
        if (i >= kWarmup) {
            samples.push_back(to_ns(Clock::now() - start));
        }
    }

    int status = 0;
    static_cast<void>(::waitpid(child, &status, 0));
    return samples;
}

// --- Shared memory ring ----------------------------------------------------

/// Two rings, one per direction. A single ring cannot ping-pong: SPSC means
/// exactly one writer and one reader, and here each side must do both.
[[noreturn]] void ring_echo_child(const std::string& to_child, const std::string& to_parent) {
    auto inbox = SharedRing<Message>::open(to_child);
    auto outbox = SharedRing<Message>::open(to_parent);
    if (!inbox || !outbox) {
        ::_exit(1);
    }
    Message message{};
    for (int i = 0; i < kWarmup + kIterations; ++i) {
        while (!inbox->try_pop(message)) {
            // Spin. This is the honest cost of shared memory: without a
            // syscall there is nothing to block on, so a busy consumer burns a
            // core. A real design pairs the ring with an eventfd for idling.
        }
        while (!outbox->try_push(message)) {
        }
    }
    ::_exit(0);
}

std::vector<double> measure_ring(const std::string& to_child, const std::string& to_parent) {
    auto outbox = SharedRing<Message>::create(to_child, 64);
    auto inbox = SharedRing<Message>::create(to_parent, 64);
    if (!outbox || !inbox) {
        return {};
    }

    const pid_t child = ::fork();
    if (child == 0) {
        ring_echo_child(to_child, to_parent);
    }

    std::vector<double> samples;
    samples.reserve(kIterations);
    Message message{};

    for (int i = 0; i < kWarmup + kIterations; ++i) {
        message.sequence = static_cast<std::uint64_t>(i);
        const auto start = Clock::now();

        while (!outbox->try_push(message)) {
        }
        while (!inbox->try_pop(message)) {
        }

        if (i >= kWarmup) {
            samples.push_back(to_ns(Clock::now() - start));
        }
    }

    int status = 0;
    static_cast<void>(::waitpid(child, &status, 0));
    return samples;
}

int run() {
    log::set_level(log::Level::Info);

    const std::string suffix = std::to_string(::getpid());
    const std::string socket_path = "/tmp/av-bench-" + suffix + ".sock";
    const std::string to_child = "/av-bench-down-" + suffix;
    const std::string to_parent = "/av-bench-up-" + suffix;

    say("");
    say("IPC round-trip, two processes, " + std::to_string(sizeof(Message)) + "-byte payload, " +
        std::to_string(kIterations) + " samples");
    say("");
    std::ostringstream header;
    header << "  " << std::left << std::setw(22) << "transport" << std::right << std::setw(13)
           << "median" << std::setw(13) << "p99" << std::setw(13) << "max";
    say(header.str());

    auto socket_samples = measure_socket(socket_path);
    if (socket_samples.empty()) {
        log::error("bench", "socket measurement failed");
        return 1;
    }
    const Summary socket_summary = summarise(socket_samples);
    report("unix socket (RTT)", socket_summary);

    auto ring_samples = measure_ring(to_child, to_parent);
    if (ring_samples.empty()) {
        log::error("bench", "shared ring measurement failed");
        return 1;
    }
    const Summary ring_summary = summarise(ring_samples);
    report("shared memory (RTT)", ring_summary);

    say("");
    std::ostringstream ratio;
    ratio << "  shared memory is " << std::fixed << std::setprecision(1)
          << (socket_summary.median_ns / ring_summary.median_ns) << "x faster at the median, "
          << (socket_summary.p99_ns / ring_summary.p99_ns) << "x at p99";
    say(ratio.str());
    say("");
    say("  The socket pays two copies and two syscalls per hop. The ring pays one");
    say("  copy and no syscall -- but it spins, so it trades a core for the latency.");
    say("");

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
