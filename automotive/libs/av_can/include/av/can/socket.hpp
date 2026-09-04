#pragma once

// SocketCAN: a real CAN bus, reached through the Berkeley socket API.
//
// Weeks 1-4 built CAN in the abstract -- bits, frames, arbitration, error
// states, all in memory. This is where it meets the kernel. The remarkable
// thing about SocketCAN is how little is new: it is socket(), bind(), read(),
// write(), the same calls week 3 used for AF_UNIX, with a different address
// family.
//
//     C++ application
//           |
//     PF_CAN socket        <- this file
//           |
//     CAN driver / vcan
//           |
//     CAN controller       <- week 4 lives here (arbitration, TEC/REC)
//           |
//     CAN_H / CAN_L
//
// Three things genuinely differ from a Unix socket, and each one has bitten
// somebody:
//
//   1. The interface name must be translated to an index with an ioctl before
//      bind() will accept it. There is no "connect to vcan0 by name".
//   2. The kernel's can_id packs *flags* into its top three bits. Reading it as
//      a plain identifier gives 0x80000123 instead of 0x123 for every extended
//      frame.
//   3. A read() returns either 16 or 72 bytes, and which one tells you whether
//      a classic or an FD frame arrived. The size is the discriminator.
//
// The payoff arrives at the end: with error frames enabled, the kernel
// delivers week 4's TEC and REC as ordinary reads. The "silent fault" that
// closed week 4 -- a node goes bus-off and the bus looks healthier -- becomes
// something a program can actually observe.

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "av/can/frame.hpp"
#include "av/ipc/unique_fd.hpp"

namespace av::can {

/// Outcome of a socket operation. WouldBlock is a normal state on a
/// non-blocking socket, not a failure -- the same contract av::ipc::IoStatus
/// established in week 3.
enum class SocketStatus : std::uint8_t {
    Ok,
    WouldBlock,
    Error,
};

/// What a read() produced. The kernel delivers bus errors through the same
/// socket as data, distinguished by a flag, so the receive path must be able
/// to return either.
enum class FrameKind : std::uint8_t {
    Data,
    BusError,
};

/// A bus error event, as reported by the controller.
///
/// `transmit_errors` and `receive_errors` are exactly the TEC and REC of week
/// 4 -- the counters that decide error active / passive / bus-off. They are the
/// evidence that distinguishes "the ECU is off the bus" from "the ECU is fine
/// and simply has nothing to say".
struct BusErrorInfo {
    std::uint32_t classes{};         ///< raw CAN_ERR_* bits, for logging
    std::uint8_t transmit_errors{};  ///< TEC
    std::uint8_t receive_errors{};   ///< REC
    bool bus_off{false};
    bool error_passive{false};
    bool error_warning{false};  ///< a counter passed 96: the early warning
};

struct ReceiveResult {
    SocketStatus status{SocketStatus::Error};
    FrameKind kind{FrameKind::Data};
    CanFrame frame{};      ///< meaningful when kind == Data
    BusErrorInfo error{};  ///< meaningful when kind == BusError
};

/// One acceptance-filter rule. A frame is delivered when
/// `(frame.id & mask) == (id & mask)`.
///
/// Filtering in the kernel rather than in the application is not an
/// optimisation detail: on a busy bus it is the difference between waking the
/// process for every frame and waking it for the four it cares about.
struct CanFilter {
    std::uint32_t id{};
    std::uint32_t mask{kStandardIdMask};
    bool extended{false};
};

class CanSocket {
public:
    /// Binds a raw CAN socket to `interface` ("vcan0", "can0").
    ///
    /// An empty name binds to *every* CAN interface, which is what `candump
    /// any` does. Useful for diagnostics, wrong for an application that must
    /// know which bus a frame came from.
    static std::optional<CanSocket> open(std::string_view interface);

    CanSocket(const CanSocket&) = delete;
    CanSocket& operator=(const CanSocket&) = delete;
    CanSocket(CanSocket&&) noexcept = default;
    CanSocket& operator=(CanSocket&&) noexcept = default;
    ~CanSocket() = default;

    /// Allows CAN-FD frames to be sent and received.
    ///
    /// Without this a socket is classic-only and the kernel *silently drops*
    /// every FD frame on the way in. No error, no counter, no log -- the
    /// frames simply are not there. It is the week-5 counterpart of week 1's
    /// wrong byte order.
    bool enable_fd();

    /// Asks the kernel to deliver bus error events as readable frames.
    ///
    /// Off by default, which is why so much production code never notices a
    /// controller going error-passive.
    bool enable_error_frames();

    /// Replaces the acceptance filter set. An empty vector means "accept
    /// nothing", not "accept everything" -- the same asymmetry the kernel has,
    /// and a genuine source of "my receiver went silent" bugs.
    bool set_filters(const std::vector<CanFilter>& filters);

    bool set_non_blocking();

    SocketStatus send(const CanFrame& frame);
    ReceiveResult receive();

    /// For av::ipc::Poller. A CAN socket is an ordinary descriptor, which is
    /// the whole point of SocketCAN: week 3's epoll loop works unchanged.
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

private:
    explicit CanSocket(ipc::UniqueFd fd) : fd_(std::move(fd)) {}

    ipc::UniqueFd fd_;
    bool fd_mode_{false};
};

/// Translates an interface name to its kernel index.
///
/// Exposed because "no such device" is the single most common SocketCAN
/// failure -- vcan is not persistent, so it disappears on every reboot -- and
/// a caller deserves to distinguish that from a permissions problem.
std::optional<unsigned> interface_index(std::string_view name) noexcept;

}  // namespace av::can
