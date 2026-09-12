#pragma once

// The server side of one service, as an application sees it.
//
// A Skeleton is given method handlers and told when to offer. It does the
// rest: the socket, a reply that echoes the request's ids, E_UNKNOWN_METHOD for
// a method nobody registered, E_WRONG_INTERFACE_VERSION for a caller built
// against a different layout, the notification session counter -- and the
// StopOffer, in its destructor.
//
// **Creating a Skeleton does not offer it.** offer() is a separate call, made
// when the application can actually answer. An offer is a promise: a server
// that offers during its own initialisation gets called before it has data,
// and everyone who believed the offer is answered E_NOT_READY.
//
// **Destroying it withdraws it.** Week 9 had to remember to send the StopOffer
// at the bottom of main(). A destructor cannot forget, and it also runs on the
// early-return paths week 9 never covered.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "av/mw/runtime.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"

namespace av::mw {

struct SkeletonConfig {
    std::uint16_t service_id{0};
    std::uint16_t instance_id{1};
    std::uint8_t interface_version{1};
    /// The address written into the offer. The server is the only process
    /// that actually knows it.
    std::string advertised_address{"127.0.0.1"};
    std::uint16_t method_port{0};
    /// Where events are published, if the service has any. An Ipv4Multicast
    /// option.
    std::optional<service::sd::Option> events;
};

/// A handler's answer: a payload, or an error code and no payload.
struct Reply {
    service::ReturnCode code{service::ReturnCode::Ok};
    Payload payload;
};

struct SkeletonStats {
    std::uint64_t requests{0};
    std::uint64_t replies{0};
    std::uint64_t errors{0};
    std::uint64_t notifications{0};
};

class Skeleton {
    struct State;

public:
    using MethodHandler = std::function<Reply(const Payload& request)>;

    static std::optional<Skeleton> create(Runtime& runtime, SkeletonConfig config);

    Skeleton(const Skeleton&) = delete;
    Skeleton& operator=(const Skeleton&) = delete;
    Skeleton(Skeleton&&) noexcept = default;
    /// Withdraws whatever this skeleton was offering before taking `other`.
    Skeleton& operator=(Skeleton&& other) noexcept;
    /// Sends the StopOffer if the service is offered.
    ~Skeleton();

    void on_method(std::uint16_t method_id, MethodHandler handler);

    void offer();
    void stop_offer();
    [[nodiscard]] bool offered() const noexcept;

    /// Publishes to the event group. Independent of offer(): an event goes to a
    /// group address whether or not anybody has found the service yet.
    void notify(std::uint16_t event_id, const Payload& payload);

    [[nodiscard]] const SkeletonStats& stats() const noexcept;

private:
    explicit Skeleton(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

}  // namespace av::mw
