#include "av/mw/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "av/eth/udp_socket.hpp"
#include "av/ipc/poller.hpp"
#include "av/log.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"

namespace av::mw {
namespace {

namespace log = av::log;
namespace sd = av::service::sd;

using eth::Endpoint;
using eth::SocketStatus;
using eth::UdpSocket;

/// FindService wildcards: "any instance", "any major version".
constexpr std::uint16_t kAnyInstance = 0xFFFF;
constexpr std::uint8_t kAnyMajorVersion = 0xFF;

std::optional<UdpSocket> open_sd_socket(const RuntimeConfig& config) {
    auto socket = UdpSocket::open();
    if (!socket) {
        return std::nullopt;
    }
    if (!socket->set_non_blocking() || !socket->bind_any(config.sd_port) ||
        !socket->join_multicast(config.sd_group, config.interface_address) ||
        !socket->set_multicast_interface(config.interface_address) ||
        !socket->set_multicast_ttl(1) || !socket->set_multicast_loopback(true)) {
        return std::nullopt;
    }
    return socket;
}

std::string sender_key(const Endpoint& from) {
    return from.address + ":" + std::to_string(from.port);
}

}  // namespace

struct Runtime::Impl {
    struct Watch {
        int fd{-1};
        ReadHandler handler;
    };
    struct Interest {
        std::uint16_t service_id{0};
        std::uint16_t instance_id{0};
        AvailabilityHandler handler;
    };
    struct Offer {
        OfferSpec spec;
        Clock::time_point last_sent;
    };

    Impl(RuntimeConfig config_in, ipc::Poller poller_in, UdpSocket sd_in)
        : config(std::move(config_in)), poller(std::move(poller_in)), sd_socket(std::move(sd_in)) {}

    RuntimeConfig config;
    ipc::Poller poller;
    UdpSocket sd_socket;
    sd::ServiceRegistry registry;
    service::SessionCounter sd_sessions;
    RuntimeStats stats;

    std::map<Id, Watch> watches;
    std::map<int, Id> watch_by_fd;
    std::map<Id, TickHandler> ticks;
    std::map<Id, Interest> interests;
    std::map<Id, Offer> offers;
    std::vector<Task> posted;
    std::map<std::string, std::uint16_t> seen_sessions;
    Clock::time_point last_find;
    std::vector<std::uint8_t> buffer;
    Id next_id{1};

    Id allocate() noexcept { return next_id++; }

    void dispatch(const std::vector<ipc::PollEvent>& ready);
    void read_sd();
    void apply(const sd::Message& message, std::uint16_t session, const Endpoint& from);
    void observe(const sd::Entry& entry, Clock::time_point now);
    void note_reboot(const sd::Message& message, std::uint16_t session, const Endpoint& from);
    void answer_find(const sd::Entry& find, Clock::time_point now);
    void notify(std::uint16_t service_id, std::uint16_t instance_id, bool available);
    void expire_leases(Clock::time_point now);
    void repeat_finds(Clock::time_point now);
    void repeat_offers(Clock::time_point now);
    void run_ticks(Clock::time_point now);
    void run_posted();
    bool send_sd(const sd::Message& message);
    bool send_offer(Offer& offer, std::uint32_t ttl, Clock::time_point now);
};

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

void Runtime::Impl::dispatch(const std::vector<ipc::PollEvent>& ready) {
    // Copy the fds out first. A handler may unwatch any fd -- including one
    // later in this very batch -- which is week 1's iterator invalidation,
    // met again inside an event loop.
    std::vector<int> fds;
    fds.reserve(ready.size());
    for (const auto& event : ready) {
        if (event.readable || event.hangup) {
            fds.push_back(event.fd);
        }
    }

    for (const int fd : fds) {
        const auto by_fd = watch_by_fd.find(fd);
        if (by_fd == watch_by_fd.end()) {
            continue;  // unwatched by an earlier handler in this batch
        }
        const auto watch = watches.find(by_fd->second);
        if (watch == watches.end()) {
            continue;
        }
        // A copy, not a reference: the owner may unwatch -- destroying this
        // very std::function -- from inside the call.
        const ReadHandler handler = watch->second.handler;
        handler();
    }
}

void Runtime::Impl::run_ticks(Clock::time_point now) {
    std::vector<Id> ids;
    ids.reserve(ticks.size());
    for (const auto& entry : ticks) {
        ids.push_back(entry.first);
    }
    for (const Id id : ids) {
        const auto found = ticks.find(id);
        if (found == ticks.end()) {
            continue;
        }
        const TickHandler handler = found->second;
        handler(now);
    }
}

void Runtime::Impl::run_posted() {
    // Swap first. A task may post another, which then runs in the *next*
    // poll rather than stretching this one indefinitely.
    std::vector<Task> due;
    due.swap(posted);
    for (const auto& task : due) {
        task();
    }
}

// ---------------------------------------------------------------------------
// Service Discovery
// ---------------------------------------------------------------------------

void Runtime::Impl::read_sd() {
    while (true) {
        const auto result = sd_socket.receive(buffer);
        if (result.status != SocketStatus::Ok) {
            return;
        }
        const auto message = service::deserialize(buffer.data(), result.size);
        if (!message || !sd::is_sd(message->header)) {
            continue;
        }
        const auto payload = sd::parse_payload(message->payload.data(), message->payload.size());
        if (!payload) {
            continue;
        }
        apply(*payload, message->header.session_id, result.from);
    }
}

void Runtime::Impl::apply(const sd::Message& message, std::uint16_t session,
                          const Endpoint& from) {
    note_reboot(message, session, from);
    const auto now = Clock::now();
    for (const auto& entry : message.entries) {
        if (entry.type == sd::EntryType::FindService) {
            answer_find(entry, now);
        } else {
            observe(entry, now);
        }
    }
}

void Runtime::Impl::observe(const sd::Entry& entry, Clock::time_point now) {
    const auto change = registry.observe(entry, now);
    if (change == sd::Availability::Available) {
        log::info("mw", "service available", "service", entry.service_id, "instance",
                  entry.instance_id);
        notify(entry.service_id, entry.instance_id, true);
    } else if (change == sd::Availability::Unavailable) {
        ++stats.stop_offers_seen;
        log::info("mw", "StopOffer -- the service was withdrawn on purpose", "service",
                  entry.service_id, "instance", entry.instance_id);
        notify(entry.service_id, entry.instance_id, false);
    }
}

void Runtime::Impl::note_reboot(const sd::Message& message, std::uint16_t session,
                                const Endpoint& from) {
    // Week 9's two lessons, kept: state is per sender, and only a sender that
    // *offers* is tracked. This runtime hears its own FindService on loopback,
    // and a question carries no state worth remembering.
    const bool offers_something =
        std::any_of(message.entries.begin(), message.entries.end(), [](const sd::Entry& entry) {
            return entry.type == sd::EntryType::OfferService;
        });
    if (!offers_something) {
        return;
    }

    const std::string sender = sender_key(from);
    const auto seen = seen_sessions.find(sender);
    if (message.reboot && seen != seen_sessions.end() && session <= seen->second) {
        ++stats.reboots_seen;
        log::warn("mw", "peer rebooted -- its session ids went backwards", "sender", sender, "was",
                  seen->second, "now", session);
    }
    seen_sessions[sender] = session;
}

void Runtime::Impl::answer_find(const sd::Entry& find, Clock::time_point now) {
    for (auto& entry : offers) {
        Offer& offer = entry.second;
        const bool service_matches = offer.spec.service_id == find.service_id;
        const bool instance_matches =
            find.instance_id == kAnyInstance || find.instance_id == offer.spec.instance_id;
        // Answered by *multicast*, not unicast back to the asker. Measured in
        // week 10: on one host, two sockets bound to the SD port with
        // SO_REUSEADDR both receive multicast, but a unicast datagram reaches
        // only the one bound last. Week 9's unicast answer therefore went back
        // to the server itself whenever the server started second. On a real
        // vehicle every ECU has its own address and unicast is fine; on one
        // host it is exactly why vsomeip puts one routing manager in front.
        if (service_matches && instance_matches && send_offer(offer, config.offer_ttl, now)) {
            ++stats.finds_answered;
        }
    }
}

void Runtime::Impl::notify(std::uint16_t service_id, std::uint16_t instance_id, bool available) {
    std::vector<Id> ids;
    for (const auto& entry : interests) {
        if (entry.second.service_id == service_id && entry.second.instance_id == instance_id) {
            ids.push_back(entry.first);
        }
    }
    for (const Id id : ids) {
        const auto found = interests.find(id);
        if (found == interests.end()) {
            continue;
        }
        const AvailabilityHandler handler = found->second.handler;
        handler(available);
    }
}

void Runtime::Impl::expire_leases(Clock::time_point now) {
    for (const auto& change : registry.expire(now)) {
        ++stats.leases_expired;
        log::warn("mw", "lease expired -- nobody said goodbye", "service", change.service_id,
                  "instance", change.instance_id);
        notify(change.service_id, change.instance_id, false);
    }
}

void Runtime::Impl::repeat_finds(Clock::time_point now) {
    if (now - last_find < config.find_interval) {
        return;
    }

    sd::Message message;
    for (const auto& entry : interests) {
        const Interest& interest = entry.second;
        if (registry.available(interest.service_id, interest.instance_id)) {
            continue;  // already known: the offers keep arriving by themselves
        }
        sd::Entry find;
        find.type = sd::EntryType::FindService;
        find.service_id = interest.service_id;
        find.instance_id = interest.instance_id;
        find.major_version = kAnyMajorVersion;
        find.ttl = config.offer_ttl;
        message.entries.push_back(find);
    }
    if (message.entries.empty()) {
        return;
    }

    last_find = now;
    if (send_sd(message)) {
        ++stats.finds_sent;
        log::debug("mw", "FindService", "entries", message.entries.size());
    }
}

void Runtime::Impl::repeat_offers(Clock::time_point now) {
    for (auto& entry : offers) {
        Offer& offer = entry.second;
        if (now - offer.last_sent >= config.offer_interval) {
            static_cast<void>(send_offer(offer, config.offer_ttl, now));
        }
    }
}

bool Runtime::Impl::send_sd(const sd::Message& message) {
    std::vector<std::uint8_t> wire;
    if (!sd::serialize(message, sd_sessions.next(), wire)) {
        return false;
    }
    const Endpoint group{config.sd_group, config.sd_port};
    return sd_socket.send_to(group, wire.data(), wire.size()) == SocketStatus::Ok;
}

bool Runtime::Impl::send_offer(Offer& offer, std::uint32_t ttl, Clock::time_point now) {
    sd::Entry entry;
    entry.type = sd::EntryType::OfferService;
    entry.service_id = offer.spec.service_id;
    entry.instance_id = offer.spec.instance_id;
    entry.major_version = offer.spec.major_version;
    entry.ttl = ttl;
    entry.options.push_back(offer.spec.method);
    if (offer.spec.events) {
        entry.options.push_back(*offer.spec.events);
    }

    sd::Message message;
    message.entries = {entry};
    if (!send_sd(message)) {
        return false;
    }
    ++stats.offers_sent;
    offer.last_sent = now;
    return true;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

std::unique_ptr<Runtime> Runtime::create(RuntimeConfig config) {
    auto poller = ipc::Poller::create();
    if (!poller) {
        log::error("mw", "could not create the poller");
        return nullptr;
    }
    auto sd_socket = open_sd_socket(config);
    if (!sd_socket) {
        log::error("mw", "could not open the SD socket", "group", config.sd_group, "port",
                   config.sd_port, "interface", config.interface_address);
        return nullptr;
    }

    auto runtime = std::make_unique<Runtime>(
        std::make_unique<Impl>(std::move(config), std::move(*poller), std::move(*sd_socket)));

    Impl* impl = runtime->impl_.get();
    if (runtime->watch(impl->sd_socket.fd(), [impl] { impl->read_sd(); }) == 0) {
        return nullptr;
    }
    return runtime;
}

Runtime::Runtime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Runtime::~Runtime() = default;

void Runtime::poll(std::chrono::milliseconds timeout) {
    Impl& impl = *impl_;
    // Completions are waiting: deliver them now rather than sleeping first.
    if (!impl.posted.empty()) {
        timeout = std::chrono::milliseconds{0};
    }

    impl.dispatch(impl.poller.wait(timeout));

    // Then everything driven by the clock rather than by a datagram. Leases
    // first, so a service that just vanished is not called in this same round.
    const auto now = Clock::now();
    impl.expire_leases(now);
    impl.repeat_finds(now);
    impl.repeat_offers(now);
    impl.run_ticks(now);
    impl.run_posted();
}

int Runtime::fd() const noexcept { return impl_->poller.fd(); }

const RuntimeConfig& Runtime::config() const noexcept { return impl_->config; }

const RuntimeStats& Runtime::stats() const noexcept { return impl_->stats; }

Runtime::Id Runtime::watch(int fd, ReadHandler handler) {
    if (!impl_->poller.watch_readable(fd)) {
        return 0;
    }
    const Id id = impl_->allocate();
    impl_->watches.emplace(id, Impl::Watch{fd, std::move(handler)});
    impl_->watch_by_fd[fd] = id;
    return id;
}

void Runtime::unwatch(Id id) {
    const auto found = impl_->watches.find(id);
    if (found == impl_->watches.end()) {
        return;
    }
    const int fd = found->second.fd;
    static_cast<void>(impl_->poller.unwatch(fd));
    const auto by_fd = impl_->watch_by_fd.find(fd);
    if (by_fd != impl_->watch_by_fd.end() && by_fd->second == id) {
        impl_->watch_by_fd.erase(by_fd);
    }
    impl_->watches.erase(found);
}

Runtime::Id Runtime::on_tick(TickHandler handler) {
    const Id id = impl_->allocate();
    impl_->ticks.emplace(id, std::move(handler));
    return id;
}

void Runtime::cancel_tick(Id id) { impl_->ticks.erase(id); }

void Runtime::post(Task task) { impl_->posted.push_back(std::move(task)); }

Runtime::Id Runtime::find(std::uint16_t service_id, std::uint16_t instance_id,
                          AvailabilityHandler handler) {
    Impl& impl = *impl_;
    const Id id = impl.allocate();
    impl.interests.emplace(id, Impl::Interest{service_id, instance_id, std::move(handler)});

    if (impl.registry.available(service_id, instance_id)) {
        // Already known. Tell the newcomer -- but on the next poll, not from
        // inside this call.
        Impl* raw = impl_.get();
        post([raw, id] {
            const auto found = raw->interests.find(id);
            if (found != raw->interests.end()) {
                const AvailabilityHandler notify = found->second.handler;
                notify(true);
            }
        });
    } else {
        // Ask at the next poll instead of a full interval from now.
        impl.last_find = Clock::time_point{};
    }
    return id;
}

void Runtime::cancel_find(Id id) { impl_->interests.erase(id); }

Runtime::Id Runtime::offer(OfferSpec spec) {
    Impl& impl = *impl_;
    const Id id = impl.allocate();
    const std::uint16_t service_id = spec.service_id;
    const std::uint16_t instance_id = spec.instance_id;
    auto& record = impl.offers.emplace(id, Impl::Offer{std::move(spec), Clock::time_point{}})
                       .first->second;
    static_cast<void>(impl.send_offer(record, impl.config.offer_ttl, Clock::now()));
    log::info("mw", "offering", "service", service_id, "instance", instance_id, "ttl_s",
              impl.config.offer_ttl);
    return id;
}

void Runtime::stop_offer(Id id) {
    Impl& impl = *impl_;
    const auto found = impl.offers.find(id);
    if (found == impl.offers.end()) {
        return;
    }
    static_cast<void>(impl.send_offer(found->second, sd::kTtlStop, Clock::now()));
    log::info("mw", "StopOffer sent", "service", found->second.spec.service_id, "instance",
              found->second.spec.instance_id);
    impl.offers.erase(found);
}

std::optional<service::sd::Option> Runtime::method_endpoint(std::uint16_t service_id,
                                                            std::uint16_t instance_id) const {
    return impl_->registry.method_endpoint(service_id, instance_id);
}

std::optional<service::sd::Option> Runtime::event_endpoint(std::uint16_t service_id,
                                                           std::uint16_t instance_id) const {
    return impl_->registry.event_endpoint(service_id, instance_id);
}

}  // namespace av::mw
