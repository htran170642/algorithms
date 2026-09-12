#include "av/mw/proxy.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "av/eth/udp_socket.hpp"
#include "av/log.hpp"
#include "av/mw/result.hpp"
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

std::chrono::microseconds since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
}

bool same_endpoint(const sd::Option& a, const sd::Option& b) {
    return a.address == b.address && a.port == b.port;
}

}  // namespace

struct Proxy::State : std::enable_shared_from_this<Proxy::State> {
    struct Pending {
        CallHandler handler;
        Clock::time_point sent_at;
    };

    State(Runtime& runtime_in, ProxyConfig config_in, UdpSocket socket_in)
        : runtime(&runtime_in), config(std::move(config_in)), method_socket(std::move(socket_in)) {}

    Runtime* runtime;
    ProxyConfig config;
    UdpSocket method_socket;
    std::optional<UdpSocket> event_socket;
    std::optional<sd::Option> joined;
    Runtime::Id method_watch{0};
    Runtime::Id event_watch{0};
    Runtime::Id tick{0};
    Runtime::Id interest{0};
    service::SessionCounter sessions;
    std::map<std::uint16_t, Pending> pending;
    std::map<std::uint16_t, EventHandler> event_handlers;
    AvailabilityHandler availability_handler;
    ProxyStats stats;
    std::vector<std::uint8_t> buffer;
    bool is_available{false};
    bool closed{false};

    bool start();
    void close();
    [[nodiscard]] std::optional<sd::Option> method_endpoint() const;
    [[nodiscard]] std::optional<sd::Option> event_endpoint() const;

    void call(std::uint16_t method_id, const Payload& request, CallHandler handler);
    void post_failure(CallHandler handler, Failure failure);
    std::optional<Pending> take(std::uint16_t session);

    void read_replies();
    void on_reply(const service::Message& message);
    void check_timeouts(Clock::time_point now);
    void fail_all();
    void on_availability_changed(bool available);

    void follow_events();
    void drop_events();
    void read_events();
};

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

bool Proxy::State::start() {
    // Every handler the runtime keeps holds a *weak* reference. The runtime
    // never keeps a proxy alive; it only finds out, at each dispatch, whether
    // the proxy still exists.
    const std::weak_ptr<State> weak = weak_from_this();

    method_watch = runtime->watch(method_socket.fd(), [weak] {
        if (const auto self = weak.lock()) {
            self->read_replies();
        }
    });
    tick = runtime->on_tick([weak](Clock::time_point now) {
        if (const auto self = weak.lock()) {
            self->check_timeouts(now);
        }
    });

    if (config.fixed_method) {
        // Believed rather than discovered, so "available" from the first
        // instant -- and still "available" after the server has died.
        is_available = true;
    } else {
        interest = runtime->find(config.service_id, config.instance_id, [weak](bool available) {
            if (const auto self = weak.lock()) {
                self->on_availability_changed(available);
            }
        });
    }
    return method_watch != 0;
}

void Proxy::State::close() {
    if (closed) {
        return;
    }
    closed = true;
    drop_events();
    runtime->unwatch(method_watch);
    runtime->cancel_tick(tick);
    if (interest != 0) {
        runtime->cancel_find(interest);
    }
    // Cancelled, not failed. Destroying the proxy is how an application says
    // it no longer wants these answers, so there is nobody left to tell.
    pending.clear();
    event_handlers.clear();
    availability_handler = nullptr;
}

std::optional<sd::Option> Proxy::State::method_endpoint() const {
    if (config.fixed_method) {
        return config.fixed_method;
    }
    return runtime->method_endpoint(config.service_id, config.instance_id);
}

std::optional<sd::Option> Proxy::State::event_endpoint() const {
    if (config.fixed_events) {
        return config.fixed_events;
    }
    return runtime->event_endpoint(config.service_id, config.instance_id);
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

void Proxy::State::call(std::uint16_t method_id, const Payload& request, CallHandler handler) {
    ++stats.calls;

    std::optional<sd::Option> endpoint;
    if (is_available) {
        endpoint = method_endpoint();
    }
    if (!endpoint) {
        // Decided here, and nothing goes on the wire. This is the case week 8
        // could only discover by sending and waiting 300 ms.
        ++stats.not_available;
        post_failure(std::move(handler), Failure::NotAvailable);
        return;
    }
    if (pending.size() >= config.max_pending) {
        ++stats.busy;
        post_failure(std::move(handler), Failure::Busy);
        return;
    }

    service::Header header;
    header.service_id = config.service_id;
    header.method_id = method_id;
    header.client_id = config.client_id;
    header.session_id = sessions.next();
    header.interface_version = config.interface_version;
    header.type = MessageType::Request;
    header.code = ReturnCode::Ok;

    // Only reachable with 65535 calls outstanding, which max_pending forbids.
    // Checked anyway: silently replacing a waiting handler would break
    // "exactly once" in the one case nobody would think to test.
    if (pending.count(header.session_id) != 0) {
        ++stats.busy;
        post_failure(std::move(handler), Failure::Busy);
        return;
    }

    std::vector<std::uint8_t> wire;
    if (!service::serialize(header, request, wire)) {
        post_failure(std::move(handler), Failure::Malformed);
        return;
    }
    const Endpoint to{endpoint->address, endpoint->port};
    if (method_socket.send_to(to, wire.data(), wire.size()) != SocketStatus::Ok) {
        ++stats.not_available;
        post_failure(std::move(handler), Failure::NotAvailable);
        return;
    }
    pending.emplace(header.session_id, Pending{std::move(handler), Clock::now()});
}

void Proxy::State::post_failure(CallHandler handler, Failure failure) {
    // Never complete from inside call(). The caller is still in the middle of
    // its own statement, and a callback that runs now runs before the code
    // the caller wrote *after* call() -- see the header, rule 2.
    const std::weak_ptr<State> weak = weak_from_this();
    runtime->post([weak, handler = std::move(handler), failure] {
        const auto self = weak.lock();
        if (self && !self->closed) {
            handler(Result<Payload>::failure(CallError{failure, ReturnCode::Ok}));
        }
    });
}

std::optional<Proxy::State::Pending> Proxy::State::take(std::uint16_t session) {
    // Out of the table *before* its handler runs. The handler may call again,
    // or destroy this proxy, and neither can then complete this call twice.
    const auto found = pending.find(session);
    if (found == pending.end()) {
        return std::nullopt;
    }
    Pending call = std::move(found->second);
    pending.erase(found);
    return call;
}

void Proxy::State::read_replies() {
    while (!closed) {
        const auto result = method_socket.receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;
        }
        const auto message = service::deserialize(buffer.data(), result.size);
        if (message) {
            on_reply(*message);
        }
    }
}

void Proxy::State::on_reply(const service::Message& message) {
    const auto& header = message.header;
    if (header.type != MessageType::Response && header.type != MessageType::Error) {
        return;
    }

    auto call = take(header.session_id);
    if (!call) {
        // A reply to a call already given up on. Without the session match it
        // would be handed to the *next* call -- week 7's stale datagram, in a
        // different hat.
        ++stats.late_replies;
        log::warn("mw", "late reply ignored -- no call is waiting for it", "service",
                  config.service_id, "session", header.session_id);
        return;
    }

    const auto latency = since(call->sent_at);
    if (header.type == MessageType::Error) {
        ++stats.remote_errors;
        call->handler(Result<Payload>::failure(CallError{Failure::Remote, header.code}, latency));
        return;
    }
    if (header.interface_version != config.interface_version) {
        // Refused rather than decoded: a different version means the payload
        // layout is no longer agreed, and decoding it anyway would produce a
        // plausible wrong number instead of an error.
        ++stats.remote_errors;
        call->handler(Result<Payload>::failure(
            CallError{Failure::WrongInterfaceVersion, ReturnCode::Ok}, latency));
        return;
    }
    ++stats.answered;
    call->handler(Result<Payload>::success(message.payload, latency));
}

void Proxy::State::check_timeouts(Clock::time_point now) {
    std::vector<std::uint16_t> expired;
    for (const auto& entry : pending) {
        if (now - entry.second.sent_at > config.timeout) {
            expired.push_back(entry.first);
        }
    }
    for (const auto session : expired) {
        if (closed) {
            return;
        }
        auto call = take(session);
        if (!call) {
            continue;
        }
        ++stats.timeouts;
        log::warn("mw", "call timed out -- lost request, lost reply, or a wedged server",
                  "service", config.service_id, "session", session, "after_ms",
                  config.timeout.count());
        call->handler(Result<Payload>::failure(CallError{Failure::Timeout, ReturnCode::Ok},
                                               since(call->sent_at)));
    }
}

void Proxy::State::fail_all() {
    // The service is gone. Letting each call sit out its own timeout would
    // deliver the same news later -- and as TIMEOUT, which says "lost datagram
    // or wedged server", the wrong news.
    std::vector<std::uint16_t> doomed;
    doomed.reserve(pending.size());
    for (const auto& entry : pending) {
        doomed.push_back(entry.first);
    }
    for (const auto session : doomed) {
        if (closed) {
            return;
        }
        auto call = take(session);
        if (!call) {
            continue;
        }
        ++stats.not_available;
        call->handler(Result<Payload>::failure(CallError{Failure::NotAvailable, ReturnCode::Ok},
                                               since(call->sent_at)));
    }
}

void Proxy::State::on_availability_changed(bool available) {
    if (closed || available == is_available) {
        return;
    }
    is_available = available;
    if (available) {
        ++stats.became_available;
        follow_events();
    } else {
        ++stats.became_unavailable;
        drop_events();
        fail_all();
    }
    if (!closed && availability_handler) {
        const AvailabilityHandler handler = availability_handler;
        handler(available);
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void Proxy::State::follow_events() {
    if (closed || !is_available || event_handlers.empty()) {
        return;
    }
    const auto endpoint = event_endpoint();
    if (!endpoint) {
        return;
    }
    if (joined && same_endpoint(*joined, *endpoint)) {
        return;
    }
    drop_events();

    const auto& interface_address = runtime->config().interface_address;
    auto socket = UdpSocket::open();
    if (!socket || !socket->set_non_blocking() || !socket->bind_any(endpoint->port) ||
        !socket->join_multicast(endpoint->address, interface_address)) {
        log::error("mw", "could not join the event group", "group", endpoint->address, "port",
                   endpoint->port);
        return;
    }
    event_socket.emplace(std::move(*socket));

    const std::weak_ptr<State> weak = weak_from_this();
    event_watch = runtime->watch(event_socket->fd(), [weak] {
        if (const auto self = weak.lock()) {
            self->read_events();
        }
    });
    joined = endpoint;
    log::info("mw", "joined the event group", "group", endpoint->address, "port", endpoint->port);
}

void Proxy::State::drop_events() {
    if (!event_socket) {
        return;
    }
    runtime->unwatch(event_watch);
    event_watch = 0;
    if (joined) {
        // Explicit rather than left to close(): the membership belongs to the
        // service's availability, and the log should say when it ended.
        static_cast<void>(event_socket->leave_multicast(joined->address,
                                                        runtime->config().interface_address));
        log::info("mw", "left the event group", "group", joined->address, "port", joined->port);
    }
    event_socket.reset();
    joined.reset();
}

void Proxy::State::read_events() {
    while (!closed && event_socket) {
        const auto result = event_socket->receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;
        }
        const auto message = service::deserialize(buffer.data(), result.size);
        if (!message || message->header.type != MessageType::Notification ||
            message->header.service_id != config.service_id) {
            continue;
        }
        const auto found = event_handlers.find(message->header.method_id);
        if (found == event_handlers.end()) {
            continue;
        }
        ++stats.events;
        const EventHandler handler = found->second;
        handler(message->payload);
    }
}

// ---------------------------------------------------------------------------
// Proxy
// ---------------------------------------------------------------------------

std::optional<Proxy> Proxy::create(Runtime& runtime, ProxyConfig config) {
    auto socket = UdpSocket::open();
    if (!socket || !socket->set_non_blocking()) {
        log::error("mw", "could not open the method socket", "service", config.service_id);
        return std::nullopt;
    }
    auto state = std::make_shared<State>(runtime, std::move(config), std::move(*socket));
    if (!state->start()) {
        state->close();
        return std::nullopt;
    }
    return Proxy{std::move(state)};
}

Proxy::Proxy(std::shared_ptr<State> state) : state_(std::move(state)) {}

Proxy& Proxy::operator=(Proxy&& other) noexcept {
    if (this != &other) {
        if (state_) {
            state_->close();
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

Proxy::~Proxy() {
    if (state_) {
        state_->close();
    }
}

void Proxy::on_availability(AvailabilityHandler handler) {
    State& state = *state_;
    state.availability_handler = std::move(handler);
    if (!state.is_available) {
        return;
    }
    // A late listener is told the current state -- on the next poll, not now.
    const std::weak_ptr<State> weak = state.weak_from_this();
    state.runtime->post([weak] {
        const auto self = weak.lock();
        if (self && !self->closed && self->is_available && self->availability_handler) {
            const AvailabilityHandler notify = self->availability_handler;
            notify(true);
        }
    });
}

void Proxy::call(std::uint16_t method_id, const Payload& request, CallHandler handler) {
    state_->call(method_id, request, std::move(handler));
}

void Proxy::subscribe(std::uint16_t event_id, EventHandler handler) {
    state_->event_handlers[event_id] = std::move(handler);
    state_->follow_events();
}

bool Proxy::available() const noexcept { return state_ && state_->is_available; }

const ProxyStats& Proxy::stats() const noexcept { return state_->stats; }

}  // namespace av::mw
