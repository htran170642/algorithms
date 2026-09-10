// VehicleService, client side: finds the service, then calls it.
//
//     ┌─────────────── svc_client ───────────────┐
//     │ socket SD: bound 30490, joined 239.10.0.9│──FindService──► the vehicle
//     │                                          │◄─OfferService──
//     │                                          │
//     │ socket A: unbound (ephemeral port)       │──GetSpeed()───► the address
//     │                                          │◄──Response────  it was told
//     │                                          │
//     │ socket B: created only once an offer     │◄─Notification── the group it
//     │           names a group to join          │                 was told
//     └──────────────────────────────────────────┘
//
// Week 8's client opened socket B at startup, bound to 30510 and joined
// 239.10.0.2, because a header said those were the numbers. This one does not
// know them at startup. It knows a *service id*, which is a contract, and it
// asks the network where that service currently lives.
//
// Three things follow, and they are the week:
//
//   * **The client has states.** SEARCHING, then AVAILABLE, and back again. No
//     call is attempted while searching, so week 8's ambiguous 300 ms timeout
//     -- which could not separate "not there yet" from "gone" -- mostly stops
//     happening, and when it does happen it means something narrower.
//   * **A lease expires.** The server renews its offer every second with a TTL
//     of three. Kill it without warning and this client reports UNAVAILABLE
//     about three seconds later, from nothing but silence.
//   * **Availability owns resources, not just log lines.** The multicast
//     membership is joined when the service appears and dropped when it goes
//     away. A subscription that outlives its service is a switch flooding a
//     port nobody reads.
//
//   ./svc_client                  # discover, then call GetSpeed every second
//   ./svc_client --static         # week 8 behaviour: trust the compiled-in address
//   ./svc_client --method 0x0009  # call a method the server does not have
//   ./svc_client --events-only    # subscribe, never ask
//
// Start this one FIRST, then the server. It waits, finds it, and starts
// calling. Then Ctrl-C the server, and compare that with killing it outright:
// two different ways a service disappears, and two different latencies.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
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
using av::service::PayloadReader;
using av::service::ReturnCode;
using av::service::SessionCounter;

using Clock = std::chrono::steady_clock;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

extern "C" void on_signal(int /*signum*/) { g_stop = 1; }

/// How long a call may go unanswered.
///
/// With discovery in front of it this timeout means something narrower than it
/// did in week 8. There it also covered "the server does not exist". Here the
/// registry has already established that the service is offered, so a timeout
/// means the request or the reply was lost, or the server is wedged -- a
/// smaller set of causes, which is what makes it useful.
constexpr auto kCallTimeout = std::chrono::milliseconds{300};

/// How often to repeat a FindService while nothing has answered.
///
/// The standard ramps this up -- fast at first, then slower, then cyclic --
/// because a hundred ECUs all retrying at a fixed rate is a broadcast storm at
/// exactly the worst moment: power-on. One interval is enough here, and the
/// ramp is the thing to remember rather than to copy.
constexpr auto kFindInterval = std::chrono::seconds{1};

struct Options {
    std::string interface_address{"127.0.0.1"};
    /// Only used with --static. Discovery supplies it otherwise.
    std::string server{"127.0.0.1"};
    std::uint16_t client_id{0x0A01};
    std::uint16_t method{vehicle::kGetSpeed};
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
    Clock::time_point sent_at;
};

struct Stats {
    std::uint64_t calls{0};
    std::uint64_t answered{0};
    std::uint64_t timed_out{0};
    std::uint64_t errors{0};
    std::uint64_t events{0};
    std::uint64_t became_available{0};
    std::uint64_t became_unavailable{0};
};

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

/// The client half of Service Discovery: ask, listen, and keep track of leases.
class Discovery {
public:
    explicit Discovery(UdpSocket socket) : socket_(std::move(socket)) {}

    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
    [[nodiscard]] bool available() const {
        return registry_.available(vehicle::kServiceId, vehicle::kInstanceId);
    }
    [[nodiscard]] std::optional<sd::Option> method_endpoint() const {
        return registry_.method_endpoint(vehicle::kServiceId, vehicle::kInstanceId);
    }
    [[nodiscard]] std::optional<sd::Option> event_endpoint() const {
        return registry_.event_endpoint(vehicle::kServiceId, vehicle::kInstanceId);
    }

    /// Repeats the question while nothing has answered it.
    ///
    /// Going quiet once the service is known is deliberate: the offers keep
    /// arriving on their own, and a client that keeps asking anyway is adding
    /// load to a network for information it already has.
    void find(Clock::time_point now) {
        if (available() || now - last_find_ < kFindInterval) {
            return;
        }
        last_find_ = now;

        sd::Entry entry;
        entry.type = sd::EntryType::FindService;
        entry.service_id = vehicle::kServiceId;
        entry.instance_id = 0xFFFF;  // any instance will do
        entry.major_version = vehicle::kInterfaceVersion;
        entry.ttl = 3;

        sd::Message message;
        message.entries = {entry};

        std::vector<std::uint8_t> wire;
        if (!sd::serialize(message, sessions_.next(), wire)) {
            return;
        }
        const Endpoint group{sd::kGroup, sd::kPort};
        if (socket_.send_to(group, wire.data(), wire.size()) == SocketStatus::Ok) {
            std::cout << "[cli]  ?  FindService  service=0x" << std::hex << vehicle::kServiceId
                      << std::dec << "  -> " << sd::kGroup << ":" << sd::kPort
                      << "   (still searching)\n"
                      << std::flush;
        }
    }

    /// Reads every SD datagram queued and applies it.
    void drain(std::vector<std::uint8_t>& buffer, Stats& stats) {
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
            note_reboot(*payload, message->header.session_id, result.from);
            for (const auto& entry : payload->entries) {
                apply(entry, stats);
            }
        }
    }

    /// Drops leases that ran out, and says so. Returns true if anything went.
    ///
    /// Nothing was received to trigger this. That is the point: it is the one
    /// failure detector in the system that works when the peer is not merely
    /// wrong but absent.
    bool expire(Clock::time_point now, Stats& stats) {
        bool lost = false;
        for (const auto& change : registry_.expire(now)) {
            ++stats.became_unavailable;
            lost = true;
            std::cout << "[cli] !!! UNAVAILABLE  service=0x" << std::hex << change.service_id
                      << std::dec << "  -- the lease ran out, nobody said goodbye\n"
                      << std::flush;
        }
        return lost;
    }

private:
    void apply(const sd::Entry& entry, Stats& stats) {
        const bool was = available();
        const auto change = registry_.observe(entry, Clock::now());
        if (change == sd::Availability::Available && !was) {
            ++stats.became_available;
            announce_found();
        } else if (change == sd::Availability::Unavailable) {
            ++stats.became_unavailable;
            std::cout << "[cli] !!! UNAVAILABLE  StopOffer received"
                      << "  -- the server withdrew the service on purpose\n"
                      << std::flush;
        }
    }

    void announce_found() const {
        const auto method = method_endpoint();
        const auto events = event_endpoint();
        std::cout << "[cli] +++ AVAILABLE    methods at "
                  << (method ? method->address : std::string{"?"}) << ":"
                  << (method ? method->port : 0) << "   events at "
                  << (events ? events->address : std::string{"?"}) << ":"
                  << (events ? events->port : 0) << "   <- learned, not compiled in\n"
                  << std::flush;
    }

    /// Detects that a *server* restarted.
    ///
    /// The reboot flag alone proves nothing -- a server that has been up for an
    /// hour still sets it, because it means "my session ids have not wrapped
    /// yet". A restart is the flag *together with* a session id that went
    /// backwards. Without this check a client keeps stale per-sender state
    /// across a reboot and quietly discards the new messages as duplicates.
    ///
    /// Two things make this narrower than it first looks, and both were found
    /// by running it rather than by reading it:
    ///
    ///   * State is kept **per sender**. One counter for the whole network
    ///     compares one ECU's sessions against another's, and every second
    ///     server looks like it just rebooted.
    ///   * Only messages that **offer** something are tracked. This client
    ///     joined the SD group and has loopback on, so it receives its own
    ///     FindService -- and comparing its own session counter with a
    ///     server's is what produced "127.0.0.1 restarted: 2 -> 1" before the
    ///     first offer had even arrived. A question carries no state worth
    ///     remembering; only an offer does.
    void note_reboot(const sd::Message& message, std::uint16_t session, const Endpoint& from) {
        const bool offers = std::any_of(
            message.entries.begin(), message.entries.end(),
            [](const sd::Entry& entry) { return entry.type == sd::EntryType::OfferService; });
        if (!offers) {
            return;
        }

        const std::string sender = from.address + ":" + std::to_string(from.port);
        const auto seen = seen_sessions_.find(sender);
        if (message.reboot && seen != seen_sessions_.end() && session <= seen->second) {
            std::cout << "[cli]  !  REBOOT       " << sender << " restarted: session went "
                      << seen->second << " -> " << session
                      << "  (discard what it told us before)\n"
                      << std::flush;
        }
        seen_sessions_[sender] = session;
    }

    UdpSocket socket_;
    sd::ServiceRegistry registry_;
    SessionCounter sessions_;
    Clock::time_point last_find_;
    /// Last SD session id seen from each sender that offers something.
    std::map<std::string, std::uint16_t> seen_sessions_;
};

// ---------------------------------------------------------------------------
// Event subscription
// ---------------------------------------------------------------------------

/// The event socket, created and destroyed as the service comes and goes.
///
/// Week 8 opened this at startup and never closed it, which was possible only
/// because the group was a compile-time constant. Once the group is learned,
/// the socket's whole lifetime belongs to the service's availability.
class EventSubscription {
public:
    /// Joins `endpoint`, replacing an existing subscription if the address or
    /// the port changed. Idempotent.
    bool subscribe(const sd::Option& endpoint, const std::string& interface_address,
                   Poller& poller) {
        if (socket_ && group_ == endpoint.address && port_ == endpoint.port) {
            return true;
        }
        unsubscribe(poller, interface_address);

        auto socket = UdpSocket::open();
        if (!socket || !socket->set_non_blocking() || !socket->bind_any(endpoint.port) ||
            !socket->join_multicast(endpoint.address, interface_address)) {
            log::error("svc.client", "could not subscribe to the event group", "group",
                       endpoint.address, "port", endpoint.port);
            return false;
        }
        if (!poller.watch_readable(socket->fd())) {
            return false;
        }
        group_ = endpoint.address;
        port_ = endpoint.port;
        socket_ = std::move(socket);
        std::cout << "[cli]  +  subscribed to " << group_ << ":" << port_
                  << "   (IGMP join, sent now that we know where to listen)\n"
                  << std::flush;
        return true;
    }

    void unsubscribe(Poller& poller, const std::string& interface_address) {
        if (!socket_) {
            return;
        }
        static_cast<void>(poller.unwatch(socket_->fd()));
        // IP_DROP_MEMBERSHIP before the close. Closing would drop it anyway;
        // being explicit is what makes the resource visibly tied to the lease.
        static_cast<void>(socket_->leave_multicast(group_, interface_address));
        std::cout << "[cli]  -  left " << group_ << ":" << port_
                  << "   (the service is gone; so is the membership)\n"
                  << std::flush;
        socket_.reset();
        group_.clear();
        port_ = 0;
    }

    /// Null while no service is offering events. Returning a pointer rather
    /// than a reference makes "there may be nothing to listen on" a fact the
    /// caller has to handle, which for most of this program's life it is.
    [[nodiscard]] UdpSocket* socket() noexcept { return socket_ ? &*socket_ : nullptr; }

private:
    std::optional<UdpSocket> socket_;
    std::string group_;
    std::uint16_t port_{0};
};

// ---------------------------------------------------------------------------
// Methods and events
// ---------------------------------------------------------------------------

void send_call(UdpSocket& socket, const Endpoint& server, const Options& options,
               SessionCounter& sessions, std::optional<PendingCall>& pending, Stats& stats) {
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
    if (socket.send_to(server, wire.data(), wire.size()) != SocketStatus::Ok) {
        return;
    }

    pending = PendingCall{header.session_id, Clock::now()};
    ++stats.calls;
    std::cout << "[cli] --> 0x" << std::hex << std::setw(4) << std::setfill('0') << options.method
              << std::dec << std::setfill(' ') << "()  session=" << std::setw(5)
              << header.session_id << "  to " << server.address << ":" << server.port << '\n'
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

    const auto latency =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - pending->sent_at);
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
    if (!pending || Clock::now() - pending->sent_at <= kCallTimeout) {
        return;
    }
    ++stats.timed_out;
    std::cout << "[cli] !!! TIMEOUT  session=" << std::setw(5) << pending->session << "  after "
              << kCallTimeout.count() << " ms -- the service is offered, so this is a lost"
              << " datagram or a wedged server\n"
              << std::flush;
    pending.reset();
}

/// Opens the discovery socket: bound to the SD port, joined to the SD group.
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

/// Where to send a request right now, or nothing if the service is not there.
///
/// With --static this always answers, which is exactly week 8: an address that
/// is believed rather than known.
std::optional<Endpoint> target(const Options& options, const std::optional<Discovery>& discovery) {
    if (!discovery) {
        return Endpoint{options.server, vehicle::kMethodPort};
    }
    const auto learned = discovery->method_endpoint();
    if (!learned) {
        return std::nullopt;
    }
    return Endpoint{learned->address, learned->port};
}

/// Routes one batch of ready file descriptors to whichever of the three
/// sockets owns it.
///
/// Three sockets, three meanings: an answer to something we asked, an event
/// nobody asked for, and news about whether the service exists at all. They
/// share a wire format and nothing else.
void dispatch(const std::vector<av::ipc::PollEvent>& ready, UdpSocket& method_socket,
              std::optional<Discovery>& discovery, EventSubscription& events,
              std::vector<std::uint8_t>& buffer, std::optional<PendingCall>& pending,
              Stats& stats) {
    UdpSocket* event_socket = events.socket();
    for (const auto& event : ready) {
        if (!event.readable) {
            continue;
        }
        if (event.fd == method_socket.fd()) {
            drain(method_socket, true, buffer, pending, stats);
        } else if (discovery && event.fd == discovery->fd()) {
            discovery->drain(buffer, stats);
        } else if (event_socket != nullptr && event.fd == event_socket->fd()) {
            drain(*event_socket, false, buffer, pending, stats);
        }
    }
}

/// Keeps the event membership in step with the service's availability.
void follow_availability(Discovery& discovery, EventSubscription& events, const Options& options,
                         Poller& epoll) {
    if (!discovery.available()) {
        events.unsubscribe(epoll, options.interface_address);
        return;
    }
    const auto group = discovery.event_endpoint();
    if (group) {
        static_cast<void>(events.subscribe(*group, options.interface_address, epoll));
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

    // Socket A: methods. Deliberately unbound -- sendto() takes an ephemeral
    // port and the server replies straight back to it.
    auto methods = UdpSocket::open();
    if (!methods || !methods->set_non_blocking()) {
        return 1;
    }

    auto poller = Poller::create();
    if (!poller) {
        return 1;
    }
    UdpSocket& method_socket = *methods;
    Poller& epoll = *poller;
    if (!epoll.watch_readable(method_socket.fd())) {
        return 1;
    }

    std::optional<Discovery> discovery;
    if (options.use_sd) {
        auto sd_socket = open_sd_socket(options.interface_address);
        if (!sd_socket) {
            log::error("svc.client", "could not open the discovery socket");
            return 1;
        }
        discovery.emplace(std::move(*sd_socket));
        if (!epoll.watch_readable(discovery->fd())) {
            return 1;
        }
    }

    EventSubscription events;
    if (!discovery) {
        // --static: the week 8 path, where the group is believed rather than
        // learned. Kept so the two can be run side by side.
        sd::Option compiled;
        compiled.address = vehicle::kEventGroup;
        compiled.port = vehicle::kEventPort;
        static_cast<void>(events.subscribe(compiled, options.interface_address, epoll));
    }

    log::info("svc.client", "starting", "service", vehicle::kServiceId, "method", options.method,
              "client_id", options.client_id, "discovery", options.use_sd, "timeout_ms",
              kCallTimeout.count());

    SessionCounter sessions;
    std::optional<PendingCall> pending;
    Stats stats;
    std::vector<std::uint8_t> buffer;
    auto last_call = Clock::now() - std::chrono::seconds{1};

    while (g_stop == 0) {
        dispatch(epoll.wait(std::chrono::milliseconds{100}), method_socket, discovery, events,
                 buffer, pending, stats);

        const auto now = Clock::now();

        if (discovery) {
            // Leases first: a service that has just vanished must not be called
            // again in this same iteration.
            if (discovery->expire(now, stats)) {
                pending.reset();
            }
            follow_availability(*discovery, events, options, epoll);
            discovery->find(now);
        }

        // Detection, not just reception -- the same discipline week 5 applied
        // to a silent CAN bus.
        expire(pending, stats);

        const auto server = target(options, discovery);
        if (!options.events_only && server && !pending &&
            now - last_call >= std::chrono::seconds{1}) {
            last_call = now;
            send_call(method_socket, *server, options, sessions, pending, stats);
        }
    }

    std::cout << "\n---------------------------------------------\n";
    std::cout << "calls made        : " << stats.calls << '\n';
    std::cout << "answered          : " << stats.answered << '\n';
    std::cout << "errors            : " << stats.errors << '\n';
    std::cout << "timed out         : " << stats.timed_out << '\n';
    std::cout << "events received   : " << stats.events << "   <- these needed no call at all\n";
    std::cout << "became available  : " << stats.became_available << '\n';
    std::cout << "became unavailable: " << stats.became_unavailable
              << "   <- learned from silence, not from a message\n";
    std::cout << std::flush;
    return 0;
}
