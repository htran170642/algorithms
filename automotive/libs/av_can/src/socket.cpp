#include "av/can/socket.hpp"

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "av/can/frame.hpp"
#include "av/ipc/unique_fd.hpp"
#include "av/log.hpp"

namespace av::can {
namespace {

namespace log = av::log;

/// The kernel's two frame sizes. A read() returns exactly one of them, and
/// which one is how you learn whether an FD frame arrived -- there is no flag
/// to consult.
constexpr std::size_t kClassicFrameBytes = sizeof(struct can_frame);
constexpr std::size_t kFdFrameBytes = sizeof(struct canfd_frame);

/// Packs our CanFrame's id and format into the kernel's can_id word.
///
/// The top three bits are flags, not identifier. Forgetting to mask them off
/// on the way back out is why so much code reports ids like 0x80000123.
canid_t to_can_id(const CanFrame& frame) noexcept {
    if (frame.extended) {
        return (frame.id & CAN_EFF_MASK) | CAN_EFF_FLAG;
    }
    return frame.id & CAN_SFF_MASK;
}

/// The inverse: strips the flags back off.
CanFrame from_can_id(canid_t raw) noexcept {
    CanFrame frame{};
    frame.extended = (raw & CAN_EFF_FLAG) != 0U;
    frame.id = frame.extended ? (raw & CAN_EFF_MASK) : (raw & CAN_SFF_MASK);
    return frame;
}

/// Turns a CAN_ERR_FLAG frame into week 4's vocabulary.
///
/// The payload layout is fixed by <linux/can/error.h>: data[1] carries the
/// controller status bits, data[6] is TEC and data[7] is REC. Those last two
/// are the same counters ErrorCounters models, arriving here straight from the
/// controller.
BusErrorInfo to_bus_error(const struct can_frame& raw) noexcept {
    BusErrorInfo info{};
    info.classes = raw.can_id & CAN_ERR_MASK;
    info.transmit_errors = raw.data[6];
    info.receive_errors = raw.data[7];

    if ((info.classes & CAN_ERR_BUSOFF) != 0U) {
        info.bus_off = true;
    }
    if ((info.classes & CAN_ERR_CRTL) != 0U) {
        const std::uint8_t control = raw.data[1];
        info.error_passive = (control & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE)) != 0U;
        info.error_warning = (control & (CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING)) != 0U;
    }
    return info;
}

}  // namespace

std::optional<unsigned> interface_index(std::string_view name) noexcept {
    if (name.empty()) {
        return 0U;  // 0 means "every CAN interface"
    }
    if (name.size() >= IFNAMSIZ) {
        return std::nullopt;
    }

    // if_nametoindex needs a NUL-terminated string; a string_view is not one.
    const std::string owned(name);
    const unsigned index = ::if_nametoindex(owned.c_str());
    if (index == 0U) {
        return std::nullopt;
    }
    return index;
}

std::optional<CanSocket> CanSocket::open(std::string_view interface) {
    const auto index = interface_index(interface);
    if (!index) {
        // By far the most common cause: vcan is not persistent, so vcan0 is
        // gone after a reboot. Say so rather than reporting a generic failure.
        log::error("can.sock", "no such CAN interface", "name", interface, "hint",
                   "sudo ./scripts/setup_vcan.sh");
        return std::nullopt;
    }

    ipc::UniqueFd fd{::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW)};
    if (!fd.valid()) {
        log::error("can.sock", "socket(PF_CAN) failed", "errno", std::strerror(errno));
        return std::nullopt;
    }

    struct sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(*index);

    // The BSD socket API is defined in terms of struct sockaddr*, so every
    // address family reaches bind() through this cast. It is the API, not a
    // shortcut.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (::bind(fd.get(), reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
        log::error("can.sock", "bind failed", "name", interface, "errno", std::strerror(errno));
        return std::nullopt;
    }

    return CanSocket{std::move(fd)};
}

bool CanSocket::enable_fd() {
    const int on = 1;
    if (::setsockopt(fd_.get(), SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on)) != 0) {
        // Fails on an interface whose MTU is 16 -- a classic-only controller,
        // or a vcan created without `mtu 72`.
        log::warn("can.sock", "CAN-FD not available on this interface", "errno",
                  std::strerror(errno));
        return false;
    }
    fd_mode_ = true;
    return true;
}

bool CanSocket::enable_error_frames() {
    // Ask for everything the controller can report. A production receiver
    // would narrow this; a learner wants to see all of it.
    const can_err_mask_t mask = CAN_ERR_MASK;
    if (::setsockopt(fd_.get(), SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &mask, sizeof(mask)) != 0) {
        log::error("can.sock", "CAN_RAW_ERR_FILTER failed", "errno", std::strerror(errno));
        return false;
    }
    return true;
}

bool CanSocket::set_filters(const std::vector<CanFilter>& filters) {
    std::vector<struct can_filter> kernel;
    kernel.reserve(filters.size());
    for (const auto& filter : filters) {
        struct can_filter entry{};
        entry.can_id = filter.extended ? (filter.id & CAN_EFF_MASK) | CAN_EFF_FLAG
                                       : (filter.id & CAN_SFF_MASK);
        entry.can_mask = filter.extended ? (filter.mask & CAN_EFF_MASK) | CAN_EFF_FLAG
                                         : (filter.mask & CAN_SFF_MASK);
        kernel.push_back(entry);
    }

    // A zero-length filter set is legal and means "deliver nothing". The
    // kernel wants a null pointer in exactly that case.
    const void* data = kernel.empty() ? nullptr : static_cast<const void*>(kernel.data());
    const auto size = static_cast<socklen_t>(kernel.size() * sizeof(struct can_filter));

    if (::setsockopt(fd_.get(), SOL_CAN_RAW, CAN_RAW_FILTER, data, size) != 0) {
        log::error("can.sock", "CAN_RAW_FILTER failed", "count", filters.size(), "errno",
                   std::strerror(errno));
        return false;
    }
    return true;
}

bool CanSocket::set_non_blocking() {
    const int flags = ::fcntl(fd_.get(), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    // Read-modify-write: clobbering the flag word would clear O_APPEND and
    // anything else the descriptor carries.
    return ::fcntl(fd_.get(), F_SETFL, flags | O_NONBLOCK) == 0;
}

SocketStatus CanSocket::send(const CanFrame& frame) {
    if (!is_valid(frame)) {
        log::error("can.sock", "refusing to send an invalid frame", "id", frame.id, "length",
                   unsigned{frame.length});
        return SocketStatus::Error;
    }

    ssize_t written = 0;

    if (frame.fd) {
        struct canfd_frame raw{};
        raw.can_id = to_can_id(frame);
        raw.len = frame.length;
        raw.flags = frame.brs ? static_cast<std::uint8_t>(CANFD_BRS) : std::uint8_t{0};
        std::memcpy(static_cast<void*>(raw.data), frame.data.data(), frame.length);
        written = ::write(fd_.get(), &raw, sizeof(raw));
    } else {
        struct can_frame raw{};
        raw.can_id = to_can_id(frame);
        raw.can_dlc = frame.length;
        std::memcpy(static_cast<void*>(raw.data), frame.data.data(), frame.length);
        written = ::write(fd_.get(), &raw, sizeof(raw));
    }

    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // The transmit queue is full. Normal under load, not a failure.
            return SocketStatus::WouldBlock;
        }
        log::error("can.sock", "write failed", "id", frame.id, "errno", std::strerror(errno));
        return SocketStatus::Error;
    }
    return SocketStatus::Ok;
}

ReceiveResult CanSocket::receive() {
    // Read into the larger of the two layouts. The number of bytes returned is
    // what tells us which one actually arrived.
    struct canfd_frame raw{};
    const ssize_t got = ::read(fd_.get(), &raw, sizeof(raw));

    ReceiveResult result{};

    if (got < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            // Nothing queued, or a signal arrived while blocked. Both mean
            // "come back later", neither means the socket is broken.
            result.status = SocketStatus::WouldBlock;
            return result;
        }
        log::error("can.sock", "read failed", "errno", std::strerror(errno));
        return result;
    }

    if (got != static_cast<ssize_t>(kClassicFrameBytes) &&
        got != static_cast<ssize_t>(kFdFrameBytes)) {
        log::error("can.sock", "short read from a CAN socket", "bytes", got);
        return result;
    }

    // An error frame is always classic-sized and carries CAN_ERR_FLAG.
    if ((raw.can_id & CAN_ERR_FLAG) != 0U) {
        struct can_frame classic{};
        std::memcpy(&classic, &raw, kClassicFrameBytes);
        result.status = SocketStatus::Ok;
        result.kind = FrameKind::BusError;
        result.error = to_bus_error(classic);
        return result;
    }

    result.status = SocketStatus::Ok;
    result.kind = FrameKind::Data;
    result.frame = from_can_id(raw.can_id);

    if (got == static_cast<ssize_t>(kFdFrameBytes)) {
        result.frame.fd = true;
        result.frame.brs = (raw.flags & CANFD_BRS) != 0U;
        result.frame.length = raw.len;
        std::memcpy(result.frame.data.data(), static_cast<const void*>(raw.data), raw.len);
    } else {
        struct can_frame classic{};
        std::memcpy(&classic, &raw, kClassicFrameBytes);
        // can_dlc is already a byte count here; the 4-bit DLC *code* never
        // reaches userspace -- the driver translates it. That is why
        // dlc_to_length() stays a week-1/week-6 concern, not a socket one.
        result.frame.length = classic.can_dlc;
        std::memcpy(result.frame.data.data(), static_cast<const void*>(classic.data),
                    classic.can_dlc);
    }

    return result;
}

}  // namespace av::can
