// VehicleService, server side: answers methods, publishes events, and now
// *offers itself* so nobody has to be told where it is.
//
//                    ┌──────────── svc_server ────────────┐
//     GetSpeed()  ──►│ port 30509, unicast                 │──► Response
//                    │                                     │
//                    │ OnSpeedChanged, every 500 ms        │──► 239.10.0.2:30510
//                    │                                     │
//                    │ OfferService, every 1 s, TTL 3 s    │──► 239.10.0.9:30490
//                    │ FindService ──────────────────────► │──► unicast offer
//                    └─────────────────────────────────────┘
//
// Week 8 had the first two rows. The third is week 9, and it changes what the
// server *is*: not a program listening on a port somebody hardcoded, but one
// that publishes a lease on its own existence.
//
//   * The offer carries **both endpoints** -- the unicast port for methods and
//     the multicast group for events -- so a client needs neither constant.
//   * The offer carries a **TTL**, and the server must keep renewing it. Kill
//     the process and no message is sent saying so; the lease simply runs out
//     and every client notices. That is how a crash becomes observable.
//   * Ctrl-C sends a **StopOffer** (TTL 0) first. A clean shutdown says so, and
//     clients react in milliseconds instead of waiting out the lease.
//
//   ./svc_server                 # answer methods, publish events, offer itself
//   ./svc_server --no-sd         # go silent on discovery -- the client never finds it
//   ./svc_server --no-events     # methods only -- watch the client go quiet
//   ./svc_server --wrong-version # answer with interface version 2
//
// Run it, kill it with Ctrl-C, and start it again while a client watches: the
// client reports UNAVAILABLE within a millisecond of the StopOffer, then
// AVAILABLE again -- and flags a reboot, because the session ids started over.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "av/eth/udp_socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"
#include "av/service/vehicle_service.hpp"

namespace {

namespace log = av::log;
namespace sd = av::service::sd;
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

/// How often the offer is repeated, and how long each one is good for.
///
/// The TTL is three times the interval on purpose. Equal values would mean a
/// single lost offer expires the lease and every client briefly believes the
/// server died; a factor of three tolerates two consecutive losses. Larger
/// still, and a real crash takes proportionally longer to notice. This ratio is
/// the whole availability/responsiveness trade, and it is one number.
constexpr auto kOfferInterval = std::chrono::seconds{1};
constexpr std::uint32_t kOfferTtl = 3;

struct Options {
    std::string interface_address{"127.0.0.1"};
    bool publish_events{true};
    bool announce{true};
    /// Answer with an interface version the client does not expect, to show
    /// what a version mismatch looks like from both ends.
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

/// Everything the server does on the discovery socket.
///
/// It owns the SD socket by value: one object holds the file descriptor, the
/// session counter and the address it advertises, and there is no way to renew
/// a lease without it.
class Announcer {
public:
    Announcer(UdpSocket socket, std::string advertised_address)
        : socket_(std::move(socket)), advertised_address_(std::move(advertised_address)) {}

    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }

    /// Repeats the offer to the SD group. Renewing the lease *is* the liveness
    /// signal -- there is no separate heartbeat, and there does not need to be.
    void announce(std::chrono::steady_clock::time_point now) {
        if (now - last_offer_ < kOfferInterval) {
            return;
        }
        last_offer_ = now;
        const Endpoint group{sd::kGroup, sd::kPort};
        if (send(offer(kOfferTtl), group)) {
            std::cout << "[srv] OfferService  service=0x" << std::hex << vehicle::kServiceId
                      << std::dec << "  ttl=" << kOfferTtl << "s  -> " << sd::kGroup << ":"
                      << sd::kPort << '\n'
                      << std::flush;
        }
    }

    /// Withdraws the offer. TTL 0 is the only message that ever says "gone".
    void stop_offer() {
        const Endpoint group{sd::kGroup, sd::kPort};
        if (send(offer(sd::kTtlStop), group)) {
            std::cout << "[srv] StopOffer     ttl=0  -- clients drop this service now\n"
                      << std::flush;
        }
    }

    /// Reads FindService requests and answers each one directly.
    ///
    /// The unicast answer is why a client that starts after the server does not
    /// have to wait up to a full offer interval: it asks, and is told.
    void serve_finds(std::vector<std::uint8_t>& buffer) {
        while (true) {
            const auto result = socket_.receive(buffer);
            if (result.status != SocketStatus::Ok) {
                return;
            }
            const auto message = av::service::deserialize(buffer.data(), result.size);
            if (!message || !sd::is_sd(message->header)) {
                continue;
            }
            const auto payload =
                sd::parse_payload(message->payload.data(), message->payload.size());
            if (!payload) {
                continue;
            }
            for (const auto& entry : payload->entries) {
                if (wanted(entry)) {
                    answer_find(result.from);
                }
            }
        }
    }

private:
    /// True when this entry is a question this server can answer.
    ///
    /// Instance 0xFFFF means "any", which is what a client asks when it does
    /// not care which of several identical ECUs replies.
    static bool wanted(const sd::Entry& entry) {
        return entry.type == sd::EntryType::FindService &&
               entry.service_id == vehicle::kServiceId &&
               (entry.instance_id == vehicle::kInstanceId || entry.instance_id == 0xFFFF);
    }

    void answer_find(const Endpoint& asker) {
        if (send(offer(kOfferTtl), asker)) {
            std::cout << "[srv] FindService from " << asker.address << ":" << asker.port
                      << "  -> unicast OfferService\n"
                      << std::flush;
        }
    }

    /// The offer itself: what this server is, and the two places to reach it.
    ///
    /// These two options are the constants that used to be read by the client
    /// out of vehicle_service.hpp. They are still written down once -- but by
    /// the server, which is the only process that actually knows them.
    [[nodiscard]] sd::Message offer(std::uint32_t ttl) const {
        sd::Option method;
        method.type = sd::OptionType::Ipv4Endpoint;
        method.address = advertised_address_;
        method.protocol = sd::Layer4::Udp;
        method.port = vehicle::kMethodPort;

        sd::Option events;
        events.type = sd::OptionType::Ipv4Multicast;
        events.address = vehicle::kEventGroup;
        events.protocol = sd::Layer4::Udp;
        events.port = vehicle::kEventPort;

        sd::Entry entry;
        entry.type = sd::EntryType::OfferService;
        entry.service_id = vehicle::kServiceId;
        entry.instance_id = vehicle::kInstanceId;
        entry.major_version = vehicle::kInterfaceVersion;
        entry.ttl = ttl;
        entry.options = {method, events};

        sd::Message message;
        // True until the session id wraps, which in a demo it never does. A
        // receiver must therefore not read the flag alone as "it rebooted": it
        // means "this sender has not been up long enough to wrap", and only a
        // session id that went *backwards* alongside it proves a restart.
        message.reboot = true;
        message.unicast = true;
        message.entries = {entry};
        return message;
    }

    bool send(const sd::Message& message, const Endpoint& to) {
        std::vector<std::uint8_t> wire;
        if (!sd::serialize(message, sessions_.next(), wire)) {
            return false;
        }
        return socket_.send_to(to, wire.data(), wire.size()) == SocketStatus::Ok;
    }

    UdpSocket socket_;
    std::string advertised_address_;
    av::service::SessionCounter sessions_;
    std::chrono::steady_clock::time_point last_offer_;
};

/// Opens the socket that both answers methods and publishes events.
///
/// One socket for two directions: replies go back to whoever asked, events go
/// to a group. Neither needs a socket of its own, because the server only ever
/// *sends* outward on this fd.
std::optional<UdpSocket> open_method_socket(const Options& options) {
    auto socket = UdpSocket::open();
    if (!socket) {
        return std::nullopt;
    }
    if (!socket->set_non_blocking() || !socket->bind_any(vehicle::kMethodPort)) {
        return std::nullopt;
    }
    if (!socket->set_multicast_interface(options.interface_address) ||
        !socket->set_multicast_ttl(1) || !socket->set_multicast_loopback(true)) {
        log::error("svc.server", "could not configure multicast for events");
        return std::nullopt;
    }
    return socket;
}

/// Opens the discovery socket: bound to the SD port, joined to the SD group,
/// and configured to send there too.
std::optional<UdpSocket> open_sd_socket(const std::string& interface_address) {
    auto socket = UdpSocket::open();
    if (!socket) {
        return std::nullopt;
    }
    if (!socket->set_non_blocking() || !socket->bind_any(sd::kPort) ||
        !socket->join_multicast(sd::kGroup, interface_address) ||
        !socket->set_multicast_interface(interface_address) || !socket->set_multicast_ttl(1) ||
        !socket->set_multicast_loopback(true)) {
        return std::nullopt;
    }
    return socket;
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

    auto socket = open_method_socket(options);
    if (!socket) {
        return 1;
    }

    std::optional<Announcer> announcer;
    if (options.announce) {
        auto sd_socket = open_sd_socket(options.interface_address);
        if (!sd_socket) {
            log::error("svc.server", "could not open the discovery socket");
            return 1;
        }
        announcer.emplace(std::move(*sd_socket), options.interface_address);
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
    if (announcer && !epoll.watch_readable(announcer->fd())) {
        return 1;
    }

    log::info("svc.server", "offering", "service", vehicle::kServiceId, "instance",
              vehicle::kInstanceId, "method_port", vehicle::kMethodPort, "event_group",
              vehicle::kEventGroup, "event_port", vehicle::kEventPort, "events",
              options.publish_events, "sd", options.announce);

    av::service::SessionCounter events;
    std::vector<std::uint8_t> buffer;
    std::uint64_t tick = 0;
    auto last_event = std::chrono::steady_clock::now();

    while (g_stop == 0) {
        const auto& ready = epoll.wait(std::chrono::milliseconds{100});
        for (const auto& event : ready) {
            if (!event.readable) {
                continue;
            }
            if (event.fd == udp.fd()) {
                drain_requests(udp, buffer, options, tick);
            } else if (announcer) {
                announcer->serve_finds(buffer);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (options.publish_events && now - last_event >= std::chrono::milliseconds{500}) {
            last_event = now;
            publish(udp, events.next(), tick);
        }
        if (announcer) {
            announcer->announce(now);
        }
        ++tick;
    }

    // Say goodbye before the socket closes. Without this the clients are
    // correct but slow: they would learn the same fact from the lease running
    // out, up to kOfferTtl seconds later.
    if (announcer) {
        announcer->stop_offer();
    }
    log::info("svc.server", "stopped");
    return 0;
}
