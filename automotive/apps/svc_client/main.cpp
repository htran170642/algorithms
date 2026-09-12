// VehicleService, client side -- rewritten in week 10 against av_mw.
//
//     week 9    711 lines: three sockets, epoll, FindService retries, leases,
//               reboot detection, session matching, timeouts, joining and
//               leaving the event group -- and ten lines of application.
//     week 10   this file: the ten lines, and the flags around them.
//
// What moved is in libs/av_mw. What the application sees of it is a contract
// (av/mw/proxy.hpp): a call ends in exactly one of seven ways, its handler runs
// exactly once, and never from inside the call that started it.
//
// What the application no longer sees is *how* the middleware knows -- the
// lease, the StopOffer, the session match, the reboot. av_mw logs those under
// the "mw" component: where somebody debugging the network will look, and where
// somebody writing a cluster screen does not have to.
//
//   ./svc_client                  # discover, then call GetSpeed every second
//   ./svc_client --static         # week 8: believe the compiled-in address
//   ./svc_client --method 0x0009  # call a method the server does not have
//   ./svc_client --events-only    # subscribe, never ask

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

#include "av/log.hpp"
#include "av/mw/proxy.hpp"
#include "av/mw/result.hpp"
#include "av/mw/runtime.hpp"
#include "av/mw/vehicle.hpp"
#include "av/service/vehicle_service.hpp"

namespace {

namespace log = av::log;
namespace mw = av::mw;
namespace contract = av::service::vehicle;

using mw::Clock;
using mw::vehicle::VehicleProxy;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

struct Options {
    std::string interface_address{"127.0.0.1"};
    /// Only used with --static. Discovery supplies it otherwise.
    std::string server{"127.0.0.1"};
    std::uint16_t client_id{0x0A01};
    std::uint16_t method{contract::kGetSpeed};
    bool events_only{false};
    bool use_sd{true};
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = (i + 1) < argc;
        if (flag == "--events-only") {
            out.events_only = true;
        } else if (flag == "--static") {
            out.use_sd = false;
        } else if (flag == "--server" && has_value) {
            out.server = argv[++i];
        } else if (flag == "--iface" && has_value) {
            out.interface_address = argv[++i];
        } else if (flag == "--method" && has_value) {
            out.method = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (flag == "--client" && has_value) {
            out.client_id = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 0));
        } else {
            std::cerr << "usage: svc_client [--server A] [--iface A] [--method 0xNNNN]"
                      << " [--client 0xNNNN] [--events-only] [--static]\n";
            return false;
        }
    }
    return true;
}

/// What each failure means, in the words an operator needs.
const char* hint(mw::Failure failure) {
    switch (failure) {
        case mw::Failure::NotAvailable:
            return "not discovered yet, or withdrawn -- nothing was sent";
        case mw::Failure::Busy:
            return "too many calls outstanding -- nothing was sent";
        case mw::Failure::Timeout:
            return "sent, no answer: lost datagram or a wedged server";
        case mw::Failure::WrongInterfaceVersion:
            return "answered in a layout we do not agree on -- not decoded";
        case mw::Failure::Malformed:
            return "the answer could not be decoded";
        case mw::Failure::Remote:
            return "the server said so";
    }
    return "";
}

void print(const mw::Result<float>& result) {
    std::cout << "[cli] <-- ";
    if (result.ok()) {
        std::cout << "RESPONSE " << std::fixed << std::setprecision(1) << std::setw(7)
                  << result.value() << "   (" << result.latency().count() << " us)\n";
    } else if (result.error().failure == mw::Failure::Remote) {
        std::cout << "ERROR " << mw::describe(result.error().code) << "   ("
                  << result.latency().count() << " us)\n";
    } else {
        std::cout << mw::describe(result.error().failure) << "   ("
                  << hint(result.error().failure) << ")\n";
    }
    std::cout << std::flush;
}

/// One question. `waiting` is cleared by the answer, whichever answer it is --
/// the contract promises exactly one.
void ask(VehicleProxy& vehicle, std::uint16_t method, bool& waiting) {
    waiting = true;
    std::cout << "[cli] --> 0x" << std::hex << std::setw(4) << std::setfill('0') << method
              << std::dec << std::setfill(' ') << "()\n"
              << std::flush;
    if (method == contract::kGetSpeed) {
        vehicle.get_speed([&waiting](const mw::Result<float>& result) {
            waiting = false;
            print(result);
        });
        return;
    }
    // Not part of the typed interface -- which is exactly how a caller reaches
    // a method the server may not have. Generated code cannot express it.
    vehicle.raw().call(method, {}, [&waiting](const mw::Result<mw::Payload>& result) {
        waiting = false;
        print(mw::vehicle::to_float(result));
    });
}

void print_stats(const mw::ProxyStats& stats) {
    std::cout << "\n---------------------------------------------\n"
              << "calls made        : " << stats.calls << '\n'
              << "answered          : " << stats.answered << '\n'
              << "remote errors     : " << stats.remote_errors << '\n'
              << "timed out         : " << stats.timeouts << '\n'
              << "not available     : " << stats.not_available
              << "   <- refused locally, nothing sent\n"
              << "busy              : " << stats.busy << '\n'
              << "late replies      : " << stats.late_replies << '\n'
              << "events received   : " << stats.events << "   <- these needed no call\n"
              << "became available  : " << stats.became_available << '\n'
              << "became unavailable: " << stats.became_unavailable << '\n'
              << std::flush;
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

    // Before the proxy, because the proxy's handlers point at it.
    bool waiting = false;

    mw::RuntimeConfig config;
    config.interface_address = options.interface_address;
    const auto runtime = mw::Runtime::create(config);
    if (!runtime) {
        return 1;
    }

    std::optional<std::string> fixed_server;
    if (!options.use_sd) {
        fixed_server = options.server;
    }
    auto vehicle = VehicleProxy::create(*runtime, options.client_id, fixed_server);
    if (!vehicle) {
        return 1;
    }

    vehicle->on_availability([](bool available) {
        std::cout << (available ? "[cli] +++ AVAILABLE    -- calls will now be sent\n"
                                : "[cli] !!! UNAVAILABLE  -- calls fail locally until it returns\n")
                  << std::flush;
    });
    vehicle->on_speed_changed([](float kmh) {
        std::cout << "[cli]  ~  EVENT    " << std::fixed << std::setprecision(1) << std::setw(7)
                  << kmh << "   (nobody asked)\n"
                  << std::flush;
    });

    auto last_call = Clock::now() - std::chrono::seconds{1};
    while (g_stop == 0) {
        runtime->poll(std::chrono::milliseconds{100});

        const auto now = Clock::now();
        if (options.events_only || waiting || now - last_call < std::chrono::seconds{1}) {
            continue;
        }
        last_call = now;
        ask(*vehicle, options.method, waiting);
    }

    print_stats(vehicle->raw().stats());
    return 0;
}
