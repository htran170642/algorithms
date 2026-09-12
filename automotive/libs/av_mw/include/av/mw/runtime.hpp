#pragma once

// The one object a process creates to take part in the vehicle's services.
//
// Weeks 8 and 9 put everything in main(): three sockets, an epoll loop, SD,
// leases, sessions, timeouts. svc_client reached 711 lines, of which about ten
// were the application. Week 10 moves the rest behind this boundary, so that an
// application can be written without knowing any of it.
//
// **The runtime owns no thread.** That is the central decision of the week.
//
//     own thread (vsomeip's choice)          no thread (this one)
//     ─────────────────────────────          ─────────────────────────────
//     callbacks arrive on its thread         callbacks arrive inside poll(),
//                                              on whichever thread calls it
//     a GUI must marshal every callback      a GUI adds fd() to its own loop
//       across to the UI thread                (QSocketNotifier, week 12)
//     a slow callback delays nothing         a slow callback delays all of
//       else in the runtime                    this process's networking
//
// The cost in the right-hand column is the rule Qt already imposes -- never
// block the event loop -- so there is one rule to learn, not two. The left-hand
// column is the right answer for a process with no event loop of its own and
// callbacks that do real work; vsomeip serves both kinds, and chose it.
//
// Using it:
//
//     auto runtime = Runtime::create({});
//     auto vehicle = VehicleProxy::create(*runtime, 0x0A01);
//     vehicle->get_speed([](const Result<float>& r) { ... });
//     while (running) runtime->poll(100ms);          // or: fd() into Qt
//
// **Ownership:** the Runtime must outlive every Proxy and Skeleton made from
// it. Declare it first and it is destroyed last -- the same rule as a Qt
// parent and its children.
//
// The members are behind a pimpl, and that is the boundary made mechanical:
// nothing in this header names a socket or a poller, so av_mw can link av_eth
// and av_ipc PRIVATE and an application cannot reach them even by accident.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "av/service/sd.hpp"

namespace av::mw {

using Clock = std::chrono::steady_clock;
using Payload = std::vector<std::uint8_t>;

struct RuntimeConfig {
    /// The NIC to join groups and send multicast on. Week 7 explains why this
    /// cannot be left to the routing table on an ECU with more than one NIC.
    std::string interface_address{"127.0.0.1"};
    /// The only address that must still be agreed in advance.
    std::string sd_group{service::sd::kGroup};
    std::uint16_t sd_port{service::sd::kPort};
    std::chrono::milliseconds find_interval{1000};
    std::chrono::milliseconds offer_interval{1000};
    /// Seconds. Three intervals, so two consecutive lost offers are survived.
    std::uint32_t offer_ttl{3};
};

/// What a Skeleton asks the runtime to advertise on its behalf.
struct OfferSpec {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{1};
    std::uint8_t major_version{1};
    service::sd::Option method;
    std::optional<service::sd::Option> events;
};

struct RuntimeStats {
    std::uint64_t offers_sent{0};
    std::uint64_t finds_sent{0};
    std::uint64_t finds_answered{0};
    std::uint64_t leases_expired{0};
    std::uint64_t stop_offers_seen{0};
    std::uint64_t reboots_seen{0};
};

class Runtime {
    struct Impl;

public:
    using Id = std::uint64_t;
    using ReadHandler = std::function<void()>;
    using TickHandler = std::function<void(Clock::time_point)>;
    using AvailabilityHandler = std::function<void(bool)>;
    using Task = std::function<void()>;

    /// Opens the SD socket and the poller. Null if either fails.
    static std::unique_ptr<Runtime> create(RuntimeConfig config);

    /// Public only so create() can use make_unique; Impl cannot be named
    /// outside this class, so nothing else can call it.
    explicit Runtime(std::unique_ptr<Impl> impl);

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    ~Runtime();

    /// Waits up to `timeout` for I/O, then does everything that is due:
    /// dispatches reads, expires leases, repeats finds and offers, checks call
    /// timeouts and delivers posted completions. Every callback runs in here.
    void poll(std::chrono::milliseconds timeout);

    /// Readable when poll() has I/O to do. For an event loop that is not this
    /// one: watch this fd, call poll(0) when it fires, *and* call it at least
    /// every 100 ms -- leases and timeouts must be checked when nothing
    /// arrives, which is exactly when they matter.
    [[nodiscard]] int fd() const noexcept;

    [[nodiscard]] const RuntimeConfig& config() const noexcept;
    [[nodiscard]] const RuntimeStats& stats() const noexcept;

    // ---- Plumbing for Proxy and Skeleton -----------------------------------
    // An application has no reason to call anything below, and the week 10
    // apps do not.

    /// Returns 0 if the fd could not be added to the poller.
    Id watch(int fd, ReadHandler handler);
    void unwatch(Id id);

    Id on_tick(TickHandler handler);
    void cancel_tick(Id id);

    /// Runs `task` inside the next poll() -- never inside the caller's stack.
    void post(Task task);

    /// Registers interest in a service. The handler hears every availability
    /// change; if the service is already known it also hears `true` once, on
    /// the next poll.
    Id find(std::uint16_t service_id, std::uint16_t instance_id, AvailabilityHandler handler);
    void cancel_find(Id id);

    /// Starts offering: sends one offer now, then repeats every interval and
    /// answers matching FindService entries.
    Id offer(OfferSpec spec);
    /// Sends the StopOffer (TTL 0) and forgets the offer.
    void stop_offer(Id id);

    [[nodiscard]] std::optional<service::sd::Option> method_endpoint(
        std::uint16_t service_id, std::uint16_t instance_id) const;
    [[nodiscard]] std::optional<service::sd::Option> event_endpoint(
        std::uint16_t service_id, std::uint16_t instance_id) const;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace av::mw
