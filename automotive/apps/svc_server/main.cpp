// VehicleService, server side -- rewritten in week 10 against av_mw.
//
//     week 9    492 lines: two sockets, an epoll loop, an SD announcer, reply
//               echoing, session counters, StopOffer on shutdown -- and a car.
//     week 10   this file: the car.
//
// Everything that left is in libs/av_mw, where every service on the vehicle
// shares one copy of it. Look at the include list: no udp_socket.hpp, no
// poller.hpp, no sd.hpp. The include list is the API boundary, and the build
// enforces it -- this target links av_mw, and av_mw keeps av_eth and av_ipc
// PRIVATE, so a socket is not reachable from here even by accident.
//
//   ./svc_server                 # answer methods, publish events, offer itself
//   ./svc_server --no-sd         # serve, but never offer -- nobody can find it
//   ./svc_server --no-events     # methods only
//   ./svc_server --wrong-version # interface version 2: v1 callers are refused
//
// Shutdown has no code. `vehicle` is declared after `runtime`, so it is
// destroyed first, and a Skeleton's destructor sends the StopOffer. Week 9 had
// to remember to call stop_offer() at the bottom of main; a destructor cannot
// forget, and it also runs on every early return.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "av/log.hpp"
#include "av/mw/runtime.hpp"
#include "av/mw/vehicle.hpp"
#include "av/service/vehicle_service.hpp"

namespace {

namespace log = av::log;
namespace mw = av::mw;
namespace contract = av::service::vehicle;

using mw::Clock;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

struct Options {
    std::string interface_address{"127.0.0.1"};
    bool publish_events{true};
    bool announce{true};
    bool wrong_version{false};
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--no-events") {
            out.publish_events = false;
        } else if (flag == "--no-sd") {
            out.announce = false;
        } else if (flag == "--wrong-version") {
            out.wrong_version = true;
        } else if (flag == "--iface" && (i + 1) < argc) {
            out.interface_address = argv[++i];
        } else {
            std::cerr << "usage: svc_server [--iface A] [--no-events] [--no-sd]"
                      << " [--wrong-version]\n";
            return false;
        }
    }
    return true;
}

/// The "vehicle". Climbs and then holds, so a stale answer is visible by eye.
float speed_at(std::uint64_t tick) {
    return static_cast<float>(std::min(40.0 + (0.5 * static_cast<double>(tick)), 200.0));
}

}  // namespace

int main(int argc, char** argv) {
    log::set_level(log::Level::Info);

    Options options;
    if (!parse_options(argc, argv, options)) {
        return 2;
    }

    static_cast<void>(std::signal(SIGINT, on_signal));
    static_cast<void>(std::signal(SIGTERM, on_signal));

    mw::RuntimeConfig config;
    config.interface_address = options.interface_address;
    const auto runtime = mw::Runtime::create(config);
    if (!runtime) {
        return 1;
    }

    const auto version =
        static_cast<std::uint8_t>(options.wrong_version ? 2U : contract::kInterfaceVersion);
    auto vehicle =
        mw::vehicle::VehicleSkeleton::create(*runtime, options.interface_address, version);
    if (!vehicle) {
        return 1;
    }

    std::uint64_t tick = 0;
    vehicle->handle_get_speed([&tick] {
        const float speed = speed_at(tick);
        std::cout << "[srv] GetSpeed() -> " << std::fixed << std::setprecision(1) << speed
                  << '\n'
                  << std::flush;
        return speed;
    });
    vehicle->handle_get_rpm([&tick] { return 800.0F + (speed_at(tick) * 20.0F); });

    // Offered only now that the handlers exist. An offer is a promise.
    if (options.announce) {
        vehicle->offer();
    }

    auto last_event = Clock::now();
    while (g_stop == 0) {
        runtime->poll(std::chrono::milliseconds{100});

        const auto now = Clock::now();
        if (options.publish_events && now - last_event >= std::chrono::milliseconds{500}) {
            last_event = now;
            vehicle->publish_speed(speed_at(tick));
            std::cout << "[srv] OnSpeedChanged " << std::fixed << std::setprecision(1)
                      << std::setw(6) << speed_at(tick) << "  -> multicast\n"
                      << std::flush;
        }
        ++tick;
    }
    return 0;
}
