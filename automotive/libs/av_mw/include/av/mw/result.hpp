#pragma once

// Every way a call can end -- as a type, not as a list of log lines.
//
// Week 8's client printed its failures as it met them. That is fine for one
// program and useless as a contract: an application built on av_mw needs to
// know, before it writes a line, every way a call can finish. There are
// exactly these:
//
//     ok                      a value, and how long it took
//     NotAvailable            discovery says the service is not there,
//                             or it went away -- nothing was sent, or the
//                             answer can no longer come
//     Busy                    too many calls already outstanding -- nothing sent
//     Timeout                 sent, and no answer came in time
//     Remote                  the server answered, with an error code
//     WrongInterfaceVersion   an answer arrived in a layout we do not agree on
//     Malformed               an answer arrived that could not be decoded
//
// NotAvailable and Busy are decided locally and cost the network nothing.
// Timeout is the only ambiguous one -- lost request, lost reply, wedged server
// -- and discovery has already made it rarer by ruling out "the server does not
// exist" before anything is sent.
//
// C++17 has no std::expected, so this is a small variant wrapper. The shape is
// the same; week 10 cares about the contract, not the spelling.

#include <chrono>
#include <cstdint>
#include <utility>
#include <variant>

#include "av/service/someip.hpp"

namespace av::mw {

enum class Failure : std::uint8_t {
    NotAvailable,
    Busy,
    Timeout,
    Remote,
    WrongInterfaceVersion,
    Malformed,
};

struct CallError {
    Failure failure{Failure::NotAvailable};
    /// Meaningful only for Failure::Remote: what the server said went wrong.
    service::ReturnCode code{service::ReturnCode::Ok};
};

[[nodiscard]] const char* describe(Failure failure) noexcept;
[[nodiscard]] const char* describe(service::ReturnCode code) noexcept;

template <typename T>
class Result {
public:
    static Result success(T value, std::chrono::microseconds latency) {
        return Result{Outcome{std::in_place_index<0>, std::move(value)}, latency};
    }

    static Result failure(CallError error,
                          std::chrono::microseconds latency = std::chrono::microseconds{0}) {
        return Result{Outcome{std::in_place_index<1>, error}, latency};
    }

    [[nodiscard]] bool ok() const noexcept { return outcome_.index() == 0; }
    [[nodiscard]] const T& value() const { return std::get<0>(outcome_); }
    [[nodiscard]] const CallError& error() const { return std::get<1>(outcome_); }

    /// From the request leaving to the answer arriving. Zero for failures
    /// decided locally, because nothing left.
    [[nodiscard]] std::chrono::microseconds latency() const noexcept { return latency_; }

private:
    using Outcome = std::variant<T, CallError>;

    Result(Outcome outcome, std::chrono::microseconds latency)
        : outcome_(std::move(outcome)), latency_(latency) {}

    Outcome outcome_;
    std::chrono::microseconds latency_;
};

}  // namespace av::mw
