#pragma once

// VehicleService as an application sees it: floats, not bytes.
//
// In a real project this file is not in the repository. It is *generated* --
// from ARXML in AUTOSAR Adaptive, from a Franca .fidl with CommonAPI -- and
// nobody reviews it line by line. It is hand-written here so its shape is
// visible, and the shape is the point: the generated layer is thin. It knows
// the method ids and how to turn a float into four big-endian bytes. It knows
// nothing about sockets, discovery, sessions or timeouts; those are in Proxy
// and Skeleton, identical for every service on the vehicle.
//
// That split is what makes a code generator possible at all. What differs
// between services is small and mechanical; what is hard is written once.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "av/mw/proxy.hpp"
#include "av/mw/result.hpp"
#include "av/mw/runtime.hpp"
#include "av/mw/skeleton.hpp"
#include "av/service/vehicle_service.hpp"

namespace av::mw::vehicle {

/// Decodes a one-float answer, keeping any error and the latency as they were.
[[nodiscard]] Result<float> to_float(const Result<Payload>& raw);

class VehicleProxy {
public:
    /// With `fixed_server` set, discovery is skipped and the week 8 constants
    /// are believed instead -- kept so the two can be run side by side.
    static std::optional<VehicleProxy> create(
        Runtime& runtime, std::uint16_t client_id,
        const std::optional<std::string>& fixed_server = std::nullopt);

    void on_availability(std::function<void(bool)> handler);
    void get_speed(std::function<void(const Result<float>&)> handler);
    void get_rpm(std::function<void(const Result<float>&)> handler);
    void on_speed_changed(std::function<void(float)> handler);

    [[nodiscard]] bool available() const noexcept { return proxy_.available(); }

    /// The untyped proxy underneath, for what the interface does not describe
    /// -- such as a method id the server may not have.
    [[nodiscard]] Proxy& raw() noexcept { return proxy_; }

private:
    explicit VehicleProxy(Proxy proxy) : proxy_(std::move(proxy)) {}

    Proxy proxy_;
};

class VehicleSkeleton {
public:
    static std::optional<VehicleSkeleton> create(
        Runtime& runtime, const std::string& advertised_address,
        std::uint8_t interface_version = service::vehicle::kInterfaceVersion);

    void handle_get_speed(std::function<float()> handler);
    void handle_get_rpm(std::function<float()> handler);
    void publish_speed(float kmh);

    void offer() { skeleton_.offer(); }
    void stop_offer() { skeleton_.stop_offer(); }

    [[nodiscard]] Skeleton& raw() noexcept { return skeleton_; }

private:
    explicit VehicleSkeleton(Skeleton skeleton) : skeleton_(std::move(skeleton)) {}

    Skeleton skeleton_;
};

}  // namespace av::mw::vehicle
