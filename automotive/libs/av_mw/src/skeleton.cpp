#include "av/mw/skeleton.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "av/eth/udp_socket.hpp"
#include "av/log.hpp"
#include "av/mw/runtime.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"

namespace av::mw {
namespace {

namespace log = av::log;
namespace sd = av::service::sd;

using eth::Endpoint;
using eth::SocketStatus;
using eth::UdpSocket;
using service::MessageType;
using service::ReturnCode;

}  // namespace

struct Skeleton::State {
    State(Runtime& runtime_in, SkeletonConfig config_in, UdpSocket socket_in)
        : runtime(&runtime_in), config(std::move(config_in)), socket(std::move(socket_in)) {}

    Runtime* runtime;
    SkeletonConfig config;
    UdpSocket socket;
    Runtime::Id watch{0};
    Runtime::Id offer_id{0};
    std::map<std::uint16_t, MethodHandler> handlers;
    service::SessionCounter notifications;
    SkeletonStats stats;
    std::vector<std::uint8_t> buffer;
    bool closed{false};

    void read_requests();
    void answer(const service::Message& request, const Endpoint& from);
    [[nodiscard]] Reply decide(const service::Message& request) const;
    void send(const service::Header& header, const Payload& payload, const Endpoint& to);
    void withdraw();
    void close();
};

void Skeleton::State::read_requests() {
    while (!closed) {
        const auto result = socket.receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;
        }
        const auto request = service::deserialize(buffer.data(), result.size);
        if (!request) {
            log::warn("mw", "not a SOME/IP message -- ignored", "bytes", result.size);
            continue;
        }
        answer(*request, result.from);
    }
}

void Skeleton::State::answer(const service::Message& request, const Endpoint& from) {
    const auto& header = request.header;
    if (header.type != MessageType::Request && header.type != MessageType::RequestNoReturn) {
        return;
    }
    ++stats.requests;
    Reply reply = decide(request);
    if (header.type == MessageType::RequestNoReturn) {
        return;  // fire and forget: even an error has nobody to go to
    }

    // The reply echoes the question -- service, method, client and session ids
    // -- and changes exactly three things. Week 8's lesson, now written once
    // for every service instead of once per server.
    service::Header out = header;
    out.interface_version = config.interface_version;
    out.code = reply.code;
    if (reply.code == ReturnCode::Ok) {
        out.type = MessageType::Response;
        ++stats.replies;
    } else {
        out.type = MessageType::Error;
        reply.payload.clear();
        ++stats.errors;
    }
    send(out, reply.payload, from);
}

Reply Skeleton::State::decide(const service::Message& request) const {
    const auto& header = request.header;
    if (header.service_id != config.service_id) {
        log::warn("mw", "request for a service not offered here", "service", header.service_id);
        return Reply{ReturnCode::UnknownService, {}};
    }
    if (header.interface_version != config.interface_version) {
        // Refused, not guessed at. The caller was built against another
        // layout; answering anyway would hand it bytes it will misread.
        log::warn("mw", "caller speaks another interface version", "theirs",
                  unsigned{header.interface_version}, "ours", unsigned{config.interface_version});
        return Reply{ReturnCode::WrongInterfaceVersion, {}};
    }
    const auto found = handlers.find(header.method_id);
    if (found == handlers.end()) {
        log::warn("mw", "no handler for this method", "service", header.service_id, "method",
                  header.method_id);
        return Reply{ReturnCode::UnknownMethod, {}};
    }
    const MethodHandler handler = found->second;
    return handler(request.payload);
}

void Skeleton::State::send(const service::Header& header, const Payload& payload,
                           const Endpoint& to) {
    std::vector<std::uint8_t> wire;
    if (service::serialize(header, payload, wire)) {
        static_cast<void>(socket.send_to(to, wire.data(), wire.size()));
    }
}

void Skeleton::State::withdraw() {
    if (offer_id == 0) {
        return;
    }
    runtime->stop_offer(offer_id);
    offer_id = 0;
}

void Skeleton::State::close() {
    if (closed) {
        return;
    }
    closed = true;
    withdraw();
    runtime->unwatch(watch);
    handlers.clear();
}

std::optional<Skeleton> Skeleton::create(Runtime& runtime, SkeletonConfig config) {
    const auto& interface_address = runtime.config().interface_address;
    auto socket = UdpSocket::open();
    // One socket for both directions: replies go back to whoever asked, events
    // go to a group. The server only ever *sends* outward on it, so neither
    // needs a socket of its own -- the asymmetry week 8 drew is unchanged.
    if (!socket || !socket->set_non_blocking() || !socket->bind_any(config.method_port) ||
        !socket->set_multicast_interface(interface_address) || !socket->set_multicast_ttl(1) ||
        !socket->set_multicast_loopback(true)) {
        log::error("mw", "could not open the service socket", "service", config.service_id,
                   "port", config.method_port);
        return std::nullopt;
    }

    auto state = std::make_shared<State>(runtime, std::move(config), std::move(*socket));
    const std::weak_ptr<State> weak = state;
    state->watch = runtime.watch(state->socket.fd(), [weak] {
        if (const auto self = weak.lock()) {
            self->read_requests();
        }
    });
    if (state->watch == 0) {
        return std::nullopt;
    }
    return Skeleton{std::move(state)};
}

Skeleton::Skeleton(std::shared_ptr<State> state) : state_(std::move(state)) {}

Skeleton& Skeleton::operator=(Skeleton&& other) noexcept {
    if (this != &other) {
        if (state_) {
            state_->close();
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

Skeleton::~Skeleton() {
    if (state_) {
        state_->close();
    }
}

void Skeleton::on_method(std::uint16_t method_id, MethodHandler handler) {
    state_->handlers[method_id] = std::move(handler);
}

void Skeleton::offer() {
    State& state = *state_;
    if (state.offer_id != 0) {
        return;
    }
    OfferSpec spec;
    spec.service_id = state.config.service_id;
    spec.instance_id = state.config.instance_id;
    spec.major_version = state.config.interface_version;
    spec.method.type = sd::OptionType::Ipv4Endpoint;
    spec.method.address = state.config.advertised_address;
    spec.method.protocol = sd::Layer4::Udp;
    spec.method.port = state.config.method_port;
    spec.events = state.config.events;
    state.offer_id = state.runtime->offer(std::move(spec));
}

void Skeleton::stop_offer() { state_->withdraw(); }

bool Skeleton::offered() const noexcept { return state_ && state_->offer_id != 0; }

void Skeleton::notify(std::uint16_t event_id, const Payload& payload) {
    State& state = *state_;
    if (!state.config.events) {
        log::error("mw", "notify() on a service with no event group", "service",
                   state.config.service_id);
        return;
    }
    service::Header header;
    header.service_id = state.config.service_id;
    header.method_id = event_id;
    // A notification answers nobody, so there is no client to name. The
    // session id still counts, so a subscriber can see a gap -- week 7 again.
    header.client_id = 0x0000;
    header.session_id = state.notifications.next();
    header.interface_version = state.config.interface_version;
    header.type = MessageType::Notification;
    header.code = ReturnCode::Ok;
    state.send(header, payload, Endpoint{state.config.events->address, state.config.events->port});
    ++state.stats.notifications;
}

const SkeletonStats& Skeleton::stats() const noexcept { return state_->stats; }

}  // namespace av::mw
