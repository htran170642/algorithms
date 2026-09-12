#pragma once

// The client side of one service, as an application sees it.
//
// A Proxy hides everything svc_client did by hand in week 9: the method socket,
// the session counter, the table of calls waiting for an answer, the timeout
// check, following the service's availability, and joining and leaving the
// event group as that availability changes.
//
// What it promises in exchange -- the contract an application is written
// against:
//
//   1. **Every call completes exactly once**, with a value or one of the
//      failures in result.hpp. Never zero times, never twice. A late reply to
//      a call that already timed out is dropped: it has nobody to go to, and
//      matching it is what the session id is for.
//   2. **Never from inside call().** Even a failure known at once
//      (NotAvailable, Busy) is delivered on the next poll. Code that writes
//          proxy.call(..., [&] { waiting = false; });
//          waiting = true;
//      would otherwise clear its flag before setting it, and wait forever.
//   3. **No automatic retry.** GetSpeed() can be sent twice harmlessly.
//      UnlockDoors() whose *reply* was lost has already run, and sending it
//      again runs it again. The middleware cannot know which methods are
//      idempotent, so the decision stays with the caller. (FindService *is*
//      retried automatically: asking again is always harmless.)
//   4. **Backpressure by refusal.** Past `max_pending` outstanding calls the
//      next one fails with Busy instead of queueing. A queue of requests to a
//      service that has died only delays the bad news.
//   5. **When the service goes away, waiting calls fail at once** with
//      NotAvailable, instead of each sitting out its own timeout -- which
//      would also report the wrong cause.
//
// Destroying a Proxy cancels: pending calls are dropped without a callback,
// because destruction is how an application says it no longer wants them. It
// is safe to do from inside one of the Proxy's own callbacks.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "av/mw/result.hpp"
#include "av/mw/runtime.hpp"
#include "av/service/sd.hpp"

namespace av::mw {

struct ProxyConfig {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{1};
    std::uint8_t interface_version{1};
    std::uint16_t client_id{0};
    std::chrono::milliseconds timeout{300};
    std::size_t max_pending{16};
    /// Week 8 mode: skip discovery and believe these instead. The proxy is then
    /// "available" from the first instant and stays so after the server has
    /// died -- the flaw week 9 removed, kept so the two can be compared.
    std::optional<service::sd::Option> fixed_method;
    std::optional<service::sd::Option> fixed_events;
};

struct ProxyStats {
    std::uint64_t calls{0};
    std::uint64_t answered{0};
    std::uint64_t remote_errors{0};
    std::uint64_t timeouts{0};
    std::uint64_t not_available{0};
    std::uint64_t busy{0};
    std::uint64_t late_replies{0};
    std::uint64_t events{0};
    std::uint64_t became_available{0};
    std::uint64_t became_unavailable{0};
};

class Proxy {
    struct State;

public:
    using CallHandler = std::function<void(const Result<Payload>&)>;
    using EventHandler = std::function<void(const Payload&)>;
    using AvailabilityHandler = std::function<void(bool)>;

    static std::optional<Proxy> create(Runtime& runtime, ProxyConfig config);

    Proxy(const Proxy&) = delete;
    Proxy& operator=(const Proxy&) = delete;
    Proxy(Proxy&&) noexcept = default;
    /// Closes this proxy's current state first: its pending calls are
    /// cancelled exactly as if it had been destroyed.
    Proxy& operator=(Proxy&& other) noexcept;
    ~Proxy();

    /// Hears every change. If the service is already available, also hears
    /// `true` once, on the next poll.
    void on_availability(AvailabilityHandler handler);

    void call(std::uint16_t method_id, const Payload& request, CallHandler handler);

    /// Joins the event group as soon as the service is available, and leaves
    /// it when the service goes away.
    void subscribe(std::uint16_t event_id, EventHandler handler);

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] const ProxyStats& stats() const noexcept;

private:
    explicit Proxy(std::shared_ptr<State> state);

    /// Shared so a callback can destroy this Proxy mid-dispatch: the code that
    /// is dispatching holds its own reference until the callback returns.
    std::shared_ptr<State> state_;
};

}  // namespace av::mw
