// VehicleService, server side: answers methods and publishes events.
//
//                    ┌──────────── svc_server ────────────┐
//     GetSpeed()  ──►│ port 30509, unicast                 │──► Response
//                    │                                     │
//                    │ OnSpeedChanged, every 500 ms        │──► 239.10.0.2:30510
//                    └─────────────────────────────────────┘        (multicast)
//
// One socket does both, and the asymmetry is the lesson:
//
//   * A **method** is a conversation. Somebody asked, exactly one somebody
//     gets the answer, and the answer echoes the Request ID so the asker can
//     tell which of its outstanding questions this replies to.
//   * An **event** is an announcement. Nobody asked, there is no reply, and
//     the server does not know or care who is listening.
//
//   ./svc_server                 # answer methods, publish events
//   ./svc_server --no-events     # methods only -- watch the client go quiet
//   ./svc_server --wrong-version # answer with interface version 2
//
// Week 7 had only the second kind. Everything CAN can express is an
// announcement; "ask a question and get an answer" has no CAN equivalent that
// is not built by hand on top of two message ids.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
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
using av::service::PayloadWriter;
using av::service::ReturnCode;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

struct Options {
    std::string interface_address{"127.0.0.1"};
    bool publish_events{true};
    /// Answer with an interface version the client does not expect, to show
    /// what a version mismatch looks like from both ends.
    bool wrong_version{false};
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--no-events") {
            out.publish_events = false;
        } else if (flag == "--wrong-version") {
            out.wrong_version = true;
        } else if (flag == "--iface" && (i + 1) < argc) {
            out.interface_address = argv[++i];
        } else {
            std::cerr << "usage: svc_server [--iface A] [--no-events] [--wrong-version]\n";
            return false;
        }
    }
    return true;
}

/// The "vehicle". Climbs and then holds, so a stale answer is visible by eye.
double speed_at(std::uint64_t tick) {
    return std::min(40.0 + (0.5 * static_cast<double>(tick)), 200.0);
}

/// Builds the reply to one request.
///
/// Note what is copied and what is decided: service id, method id, client id
/// and session id are *echoed*, because they identify the question. Only the
/// type, the return code and the payload are the answer.
Header reply_header(const Header& request, MessageType type, ReturnCode code,
                    std::uint8_t interface_version) {
    Header reply = request;
    reply.type = type;
    reply.code = code;
    reply.interface_version = interface_version;
    return reply;
}

void send_message(UdpSocket& socket, const Endpoint& to, const Header& header,
                  const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> wire;
    if (av::service::serialize(header, payload, wire)) {
        static_cast<void>(socket.send_to(to, wire.data(), wire.size()));
    }
}

void answer(UdpSocket& socket, const Endpoint& to, const Message& request, const Options& options,
            std::uint64_t tick) {
    const std::uint8_t version = options.wrong_version ? 2U : vehicle::kInterfaceVersion;

    if (request.header.service_id != vehicle::kServiceId) {
        // A different service on this port. Not a crash and not silence: the
        // caller is told which of its assumptions was wrong.
        send_message(
            socket, to,
            reply_header(request.header, MessageType::Error, ReturnCode::UnknownService, version),
            {});
        std::cout << "[srv] service 0x" << std::hex << request.header.service_id << std::dec
                  << "  -> ERROR E_UNKNOWN_SERVICE\n"
                  << std::flush;
        return;
    }

    PayloadWriter payload;
    MessageType type = MessageType::Response;
    ReturnCode code = ReturnCode::Ok;
    const char* name = "??";

    switch (request.header.method_id) {
        case vehicle::kGetSpeed:
            payload.put_f32(static_cast<float>(speed_at(tick)));
            name = "GetSpeed";
            break;
        case vehicle::kGetRpm:
            payload.put_f32(static_cast<float>(800.0 + (speed_at(tick) * 20.0)));
            name = "GetRpm";
            break;
        default:
            // Well-formed, but this server does not implement it.
            // E_UNKNOWN_METHOD says exactly that -- a different fact from "the
            // service is missing" and from "it failed".
            type = MessageType::Error;
            code = ReturnCode::UnknownMethod;
            break;
    }

    send_message(socket, to, reply_header(request.header, type, code, version), payload.bytes());

    std::cout << "[srv] " << std::setw(8) << name << "()  session=" << std::setw(5)
              << request.header.session_id << "  client=0x" << std::hex
              << request.header.client_id << std::dec << "  -> "
              << (type == MessageType::Response ? "RESPONSE" : "ERROR E_UNKNOWN_METHOD") << '\n'
              << std::flush;
}

void publish(UdpSocket& socket, std::uint16_t session, std::uint64_t tick) {
    PayloadWriter payload;
    payload.put_f32(static_cast<float>(speed_at(tick)));

    Header header;
    header.service_id = vehicle::kServiceId;
    header.method_id = vehicle::kOnSpeedChanged;
    // A notification answers nobody, so there is no client to name. 0x0000 is
    // the conventional "no client" value; the session id still counts up, so a
    // subscriber can detect a gap exactly as week 7 did.
    header.client_id = 0x0000;
    header.session_id = session;
    header.interface_version = vehicle::kInterfaceVersion;
    header.type = MessageType::Notification;
    header.code = ReturnCode::Ok;

    std::vector<std::uint8_t> wire;
    if (!av::service::serialize(header, payload.bytes(), wire)) {
        return;
    }
    const Endpoint group{vehicle::kEventGroup, vehicle::kEventPort};
    if (socket.send_to(group, wire.data(), wire.size()) == SocketStatus::Ok) {
        std::cout << "[srv] OnSpeedChanged  session=" << std::setw(5) << session
                  << "  speed=" << std::fixed << std::setprecision(1) << std::setw(6)
                  << speed_at(tick) << "  -> multicast\n"
                  << std::flush;
    }
}

/// Reads every request queued on the socket and answers each one.
///
/// Split out of main so the loop reads as "wait, then serve, then maybe
/// publish" rather than as four nested levels of control flow.
void drain_requests(UdpSocket& udp, std::vector<std::uint8_t>& buffer, const Options& options,
                    std::uint64_t tick) {
    while (true) {
        const auto result = udp.receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;  // WouldBlock: queue empty. Error: already logged.
        }
        const auto request = av::service::deserialize(buffer.data(), result.size);
        if (!request) {
            log::warn("svc.server", "not a SOME/IP message -- ignored", "bytes", result.size);
            continue;
        }
        answer(udp, result.from, *request, options, tick);
    }
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

    auto socket = UdpSocket::open();
    if (!socket) {
        return 1;
    }
    if (!socket->set_non_blocking() || !socket->bind_any(vehicle::kMethodPort)) {
        return 1;
    }
    if (!socket->set_multicast_interface(options.interface_address) ||
        !socket->set_multicast_ttl(1) || !socket->set_multicast_loopback(true)) {
        log::error("svc.server", "could not configure multicast for events");
        return 1;
    }

    auto poller = Poller::create();
    if (!poller) {
        return 1;
    }

    UdpSocket& udp = *socket;
    Poller& epoll = *poller;
    if (!epoll.watch_readable(udp.fd())) {
        return 1;
    }

    log::info("svc.server", "offering", "service", vehicle::kServiceId, "method_port",
              vehicle::kMethodPort, "event_group", vehicle::kEventGroup, "event_port",
              vehicle::kEventPort, "events", options.publish_events);

    av::service::SessionCounter events;
    std::vector<std::uint8_t> buffer;
    std::uint64_t tick = 0;
    auto last_event = std::chrono::steady_clock::now();

    while (g_stop == 0) {
        const auto& ready = epoll.wait(std::chrono::milliseconds{100});
        for (const auto& event : ready) {
            if (event.readable) {
                drain_requests(udp, buffer, options, tick);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (options.publish_events && now - last_event >= std::chrono::milliseconds{500}) {
            last_event = now;
            publish(udp, events.next(), tick);
        }
        ++tick;
    }

    log::info("svc.server", "stopped");
    return 0;
}
