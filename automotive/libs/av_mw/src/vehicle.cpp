#include "av/mw/vehicle.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "av/mw/proxy.hpp"
#include "av/mw/result.hpp"
#include "av/mw/runtime.hpp"
#include "av/mw/skeleton.hpp"
#include "av/service/sd.hpp"
#include "av/service/someip.hpp"
#include "av/service/vehicle_service.hpp"

namespace av::mw::vehicle {
namespace {

namespace contract = av::service::vehicle;
namespace sd = av::service::sd;

using FloatHandler = std::function<void(const Result<float>&)>;

sd::Option endpoint(sd::OptionType type, const std::string& address, std::uint16_t port) {
    sd::Option option;
    option.type = type;
    option.address = address;
    option.protocol = sd::Layer4::Udp;
    option.port = port;
    return option;
}

/// One float, big-endian: the entire payload format of this interface.
Payload encode(float value) {
    service::PayloadWriter writer;
    writer.put_f32(value);
    return writer.bytes();
}

void call_float(Proxy& proxy, std::uint16_t method_id, FloatHandler handler) {
    proxy.call(method_id, {}, [handler = std::move(handler)](const Result<Payload>& raw) {
        handler(to_float(raw));
    });
}

Skeleton::MethodHandler answer_float(std::function<float()> produce) {
    return [produce = std::move(produce)](const Payload& /*request*/) {
        return Reply{service::ReturnCode::Ok, encode(produce())};
    };
}

}  // namespace

Result<float> to_float(const Result<Payload>& raw) {
    if (!raw.ok()) {
        return Result<float>::failure(raw.error(), raw.latency());
    }
    service::PayloadReader reader{raw.value().data(), raw.value().size()};
    const auto value = reader.get_f32();
    if (!value) {
        return Result<float>::failure(CallError{Failure::Malformed, service::ReturnCode::Ok},
                                      raw.latency());
    }
    return Result<float>::success(*value, raw.latency());
}

std::optional<VehicleProxy> VehicleProxy::create(Runtime& runtime, std::uint16_t client_id,
                                                 const std::optional<std::string>& fixed_server) {
    ProxyConfig config;
    config.service_id = contract::kServiceId;
    config.instance_id = contract::kInstanceId;
    config.interface_version = contract::kInterfaceVersion;
    config.client_id = client_id;
    if (fixed_server) {
        config.fixed_method =
            endpoint(sd::OptionType::Ipv4Endpoint, *fixed_server, contract::kMethodPort);
        config.fixed_events =
            endpoint(sd::OptionType::Ipv4Multicast, contract::kEventGroup, contract::kEventPort);
    }
    auto proxy = Proxy::create(runtime, std::move(config));
    if (!proxy) {
        return std::nullopt;
    }
    return VehicleProxy{std::move(*proxy)};
}

void VehicleProxy::on_availability(std::function<void(bool)> handler) {
    proxy_.on_availability(std::move(handler));
}

void VehicleProxy::get_speed(FloatHandler handler) {
    call_float(proxy_, contract::kGetSpeed, std::move(handler));
}

void VehicleProxy::get_rpm(FloatHandler handler) {
    call_float(proxy_, contract::kGetRpm, std::move(handler));
}

void VehicleProxy::on_speed_changed(std::function<void(float)> handler) {
    proxy_.subscribe(contract::kOnSpeedChanged,
                     [handler = std::move(handler)](const Payload& payload) {
                         service::PayloadReader reader{payload.data(), payload.size()};
                         const auto value = reader.get_f32();
                         if (value) {
                             handler(*value);
                         }
                     });
}

std::optional<VehicleSkeleton> VehicleSkeleton::create(Runtime& runtime,
                                                       const std::string& advertised_address,
                                                       std::uint8_t interface_version) {
    SkeletonConfig config;
    config.service_id = contract::kServiceId;
    config.instance_id = contract::kInstanceId;
    config.interface_version = interface_version;
    config.advertised_address = advertised_address;
    config.method_port = contract::kMethodPort;
    config.events =
        endpoint(sd::OptionType::Ipv4Multicast, contract::kEventGroup, contract::kEventPort);
    auto skeleton = Skeleton::create(runtime, std::move(config));
    if (!skeleton) {
        return std::nullopt;
    }
    return VehicleSkeleton{std::move(*skeleton)};
}

void VehicleSkeleton::handle_get_speed(std::function<float()> handler) {
    skeleton_.on_method(contract::kGetSpeed, answer_float(std::move(handler)));
}

void VehicleSkeleton::handle_get_rpm(std::function<float()> handler) {
    skeleton_.on_method(contract::kGetRpm, answer_float(std::move(handler)));
}

void VehicleSkeleton::publish_speed(float kmh) {
    skeleton_.notify(contract::kOnSpeedChanged, encode(kmh));
}

}  // namespace av::mw::vehicle
