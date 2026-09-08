// VehicleService, client side: calls methods and subscribes to events.
//
//     ┌─────────── svc_client ───────────┐
//     │ socket A: unbound                │──GetSpeed()──► 127.0.0.1:30509
//     │           (ephemeral port)       │◄──Response────
//     │                                  │
//     │ socket B: bound 30510            │◄──Notification── 239.10.0.2:30510
//     │           joined 239.10.0.2      │
//     └──────────────────────────────────┘
//
// **Two sockets, on purpose.** A request needs a socket the server can reply
// *to*, which UDP gives free: sendto() on an unbound socket takes an ephemeral
// port and the reply comes back to it. An event needs a socket bound to the
// agreed port and joined to the group, because the server addresses the group
// and not anybody in particular.
//
// Both could be forced through one socket. Keeping them apart puts the two
// communication models in the code rather than only in the comments.
//
//   ./svc_client                  # call GetSpeed every second, print events
//   ./svc_client --method 0x0009  # call a method the server does not have
//   ./svc_client --events-only    # subscribe, never ask
//
// Run it with the server stopped and watch the timeout fire. That is the
// failure CAN cannot have: on a bus no question is ever outstanding, so there
// is nothing to time out.

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "av/eth/udp_socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"
#include "av/service/someip.hpp"
#include "av/service/vehicle_service.hpp"

namespace {

namespace log = av::log;
namespace vehicle = av::service::vehicle;

using av::eth::Endpoint;
using av::eth::SocketStatus;
using av::eth::UdpSocket;
using av::ipc::Poller;
using av::service::Header;
using av::service::Message;
using av::service::MessageType;
using av::service::PayloadReader;
using av::service::ReturnCode;
using av::service::SessionCounter;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// How long a call may go unanswered. A request that never gets a reply is the
/// normal failure of request/response over UDP -- the datagram may have been
/// lost in either direction, and neither is distinguishable from a server that
/// is simply gone.
constexpr auto kCallTimeout = std::chrono::milliseconds{300};

struct Options {
    std::string server{"127.0.0.1"};
    std::string interface_address{"127.0.0.1"};
    std::uint16_t client_id{0x0A01};
    std::uint16_t method{vehicle::kGetSpeed};
    bool events_only{false};
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool has_value = (i + 1) < argc;
        if (flag == "--events-only") {
            out.events_only = true;
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
                      << " [--client 0xNNNN] [--events-only]\n";
            return false;
        }
    }
    return true;
}

const char* code_name(ReturnCode code) {
    switch (code) {
        case ReturnCode::Ok:
            return "E_OK";
        case ReturnCode::NotOk:
            return "E_NOT_OK";
        case ReturnCode::UnknownService:
            return "E_UNKNOWN_SERVICE";
        case ReturnCode::UnknownMethod:
            return "E_UNKNOWN_METHOD";
        case ReturnCode::NotReady:
            return "E_NOT_READY";
        case ReturnCode::WrongProtocolVersion:
            return "E_WRONG_PROTOCOL_VERSION";
        case ReturnCode::WrongInterfaceVersion:
            return "E_WRONG_INTERFACE_VERSION";
        case ReturnCode::WrongMessageType:
            return "E_WRONG_MESSAGE_TYPE";
    }
    return "??";
}

/// One question waiting for its answer.
struct PendingCall {
    std::uint16_t session{0};
    std::chrono::steady_clock::time_point sent_at;
};

struct Stats {
    std::uint64_t calls{0};
    std::uint64_t answered{0};
    std::uint64_t timed_out{0};
    std::uint64_t errors{0};
    std::uint64_t events{0};
};

void send_call(UdpSocket& socket, const Options& options, SessionCounter& sessions,
               std::optional<PendingCall>& pending, Stats& stats) {
    Header header;
    header.service_id = vehicle::kServiceId;
    header.method_id = options.method;
    header.client_id = options.client_id;
    header.session_id = sessions.next();
    header.interface_version = vehicle::kInterfaceVersion;
    header.type = MessageType::Request;
    header.code = ReturnCode::Ok;

    std::vector<std::uint8_t> wire;
    if (!av::service::serialize(header, {}, wire)) {
        return;
    }

    const Endpoint server{options.server, vehicle::kMethodPort};
    if (socket.send_to(server, wire.data(), wire.size()) != SocketStatus::Ok) {
        return;
    }

    pending = PendingCall{header.session_id, std::chrono::steady_clock::now()};
    ++stats.calls;
    std::cout << "[cli] --> 0x" << std::hex << std::setw(4) << std::setfill('0') << options.method
              << std::dec << std::setfill(' ') << "()  session=" << std::setw(5)
              << header.session_id << '\n'
              << std::flush;
}

void on_reply(const Message& message, std::optional<PendingCall>& pending, Stats& stats) {
    if (message.header.type != MessageType::Response &&
        message.header.type != MessageType::Error) {
        log::warn("svc.client", "unexpected message type on the method socket");
        return;
    }

    // Matching on the session id is why the field exists. Without it, a late
    // reply to a question already given up on would be applied to the *next*
    // question -- week 7's stale datagram, wearing a different hat.
    if (!pending || pending->session != message.header.session_id) {
        std::cout << "[cli] <-- session=" << std::setw(5) << message.header.session_id
                  << "  IGNORED: no call is waiting for this (late reply)\n"
                  << std::flush;
        return;
    }

    const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - pending->sent_at);
    pending.reset();

    if (message.header.type == MessageType::Error) {
        ++stats.errors;
        std::cout << "[cli] <-- ERROR " << code_name(message.header.code) << "   ("
                  << latency.count() << " us)\n"
                  << std::flush;
        return;
    }
    if (message.header.interface_version != vehicle::kInterfaceVersion) {
        ++stats.errors;
        std::cout << "[cli] <-- RESPONSE but interface version "
                  << unsigned{message.header.interface_version}
                  << " != " << unsigned{vehicle::kInterfaceVersion}
                  << " -- payload NOT decoded\n"
                  << std::flush;
        return;
    }

    PayloadReader reader{message.payload.data(), message.payload.size()};
    const auto value = reader.get_f32();
    if (!value) {
        ++stats.errors;
        std::cout << "[cli] <-- RESPONSE with a payload too short to hold a float\n" << std::flush;
        return;
    }

    ++stats.answered;
    std::cout << "[cli] <-- RESPONSE " << std::fixed << std::setprecision(1) << std::setw(7)
              << *value << "   (" << latency.count() << " us)\n"
              << std::flush;
}

void on_event(const Message& message, Stats& stats) {
    if (message.header.type != MessageType::Notification) {
        return;
    }
    PayloadReader reader{message.payload.data(), message.payload.size()};
    const auto value = reader.get_f32();
    if (!value) {
        return;
    }
    ++stats.events;
    std::cout << "[cli]  ~  EVENT    " << std::fixed << std::setprecision(1) << std::setw(7)
              << *value << "   session=" << message.header.session_id << "  (nobody asked)\n"
              << std::flush;
}

/// Drains one socket. `is_method` decides which of the two models the bytes
/// belong to -- the same wire format, read with different expectations.
void drain(UdpSocket& socket, bool is_method, std::vector<std::uint8_t>& buffer,
           std::optional<PendingCall>& pending, Stats& stats) {
    while (true) {
        const auto result = socket.receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;
        }
        const auto message = av::service::deserialize(buffer.data(), result.size);
        if (!message) {
            continue;
        }
        if (is_method) {
            on_reply(*message, pending, stats);
        } else {
            on_event(*message, stats);
        }
    }
}

/// Gives up on a call that was never answered.
void expire(std::optional<PendingCall>& pending, Stats& stats) {
    if (!pending || std::chrono::steady_clock::now() - pending->sent_at <= kCallTimeout) {
        return;
    }
    ++stats.timed_out;
    std::cout << "[cli] !!! TIMEOUT  session=" << std::setw(5) << pending->session << "  after "
              << kCallTimeout.count() << " ms -- no answer, and no way to know why\n"
              << std::flush;
    pending.reset();
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

    // Socket A: methods. Deliberately unbound -- sendto() takes an ephemeral
    // port and the server replies straight back to it.
    auto methods = UdpSocket::open();
    // Socket B: events. Bound and joined, because the server addresses the
    // group, not this process.
    auto events = UdpSocket::open();
    if (!methods || !events) {
        return 1;
    }
    if (!methods->set_non_blocking() || !events->set_non_blocking()) {
        return 1;
    }
    if (!events->bind_any(vehicle::kEventPort) ||
        !events->join_multicast(vehicle::kEventGroup, options.interface_address)) {
        return 1;
    }

    auto poller = Poller::create();
    if (!poller) {
        return 1;
    }

    UdpSocket& method_socket = *methods;
    UdpSocket& event_socket = *events;
    Poller& epoll = *poller;
    if (!epoll.watch_readable(method_socket.fd()) || !epoll.watch_readable(event_socket.fd())) {
        return 1;
    }

    log::info("svc.client", "calling", "service", vehicle::kServiceId, "server", options.server,
              "method", options.method, "client_id", options.client_id, "event_group",
              vehicle::kEventGroup, "timeout_ms", kCallTimeout.count());

    SessionCounter sessions;
    std::optional<PendingCall> pending;
    Stats stats;
    std::vector<std::uint8_t> buffer;
    auto last_call = std::chrono::steady_clock::now() - std::chrono::seconds{1};

    while (g_stop == 0) {
        const auto& ready = epoll.wait(std::chrono::milliseconds{100});
        for (const auto& event : ready) {
            if (event.readable) {
                const bool is_method = event.fd == method_socket.fd();
                drain(is_method ? method_socket : event_socket, is_method, buffer, pending, stats);
            }
        }

        // Detection, not just reception -- the same discipline week 5 applied
        // to a silent CAN bus. A call with no answer is a fact worth logging.
        expire(pending, stats);

        const auto now = std::chrono::steady_clock::now();
        if (!options.events_only && !pending && now - last_call >= std::chrono::seconds{1}) {
            last_call = now;
            send_call(method_socket, options, sessions, pending, stats);
        }
    }

    std::cout << "\n---------------------------------------------\n";
    std::cout << "calls made     : " << stats.calls << '\n';
    std::cout << "answered       : " << stats.answered << '\n';
    std::cout << "errors         : " << stats.errors << '\n';
    std::cout << "timed out      : " << stats.timed_out << '\n';
    std::cout << "events received: " << stats.events << "   <- these needed no call at all\n";
    std::cout << std::flush;
    return 0;
}
