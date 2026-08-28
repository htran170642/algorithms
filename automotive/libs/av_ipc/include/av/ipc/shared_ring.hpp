#pragma once

// A single-producer / single-consumer ring buffer in POSIX shared memory.
//
// This is the other half of week 3. A Unix socket copies every message twice
// (user -> kernel -> user) and costs a syscall each way. Shared memory copies
// once, into a page both processes already have mapped, and costs no syscall
// at all on the fast path. The price is that you now own the synchronisation
// yourself -- there is no kernel to serialise anything for you.
//
// Layout, identical in both processes because both map the same pages:
//
//     offset 0    RingHeader  (head and tail on separate cache lines)
//     offset N    capacity * sizeof(T) bytes of slots
//
// Constraints that follow from "the same bytes are read by another process":
//   * T must be trivially copyable -- a pointer or a std::string inside it
//     would refer to an address that means nothing in the peer.
//   * The atomics must be lock-free, or they would fall back on a per-process
//     lock table and the two processes would not agree on anything.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "av/ipc/unique_fd.hpp"

namespace av::ipc {

/// Owns one shm_open + mmap region and tears it down in the right order.
///
/// Only the creator unlinks the name; a peer that merely opened it must not,
/// or a restart of the creator would race against the name disappearing.
class SharedMapping {
public:
    static std::optional<SharedMapping> create(std::string_view name, std::size_t size);
    static std::optional<SharedMapping> open(std::string_view name);

    SharedMapping(const SharedMapping&) = delete;
    SharedMapping& operator=(const SharedMapping&) = delete;
    SharedMapping(SharedMapping&& other) noexcept;
    SharedMapping& operator=(SharedMapping&& other) noexcept;
    ~SharedMapping();

    [[nodiscard]] void* address() const noexcept { return address_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    SharedMapping(UniqueFd fd, void* address, std::size_t size, std::string owned_name)
        : fd_(std::move(fd)),
          address_(address),
          size_(size),
          owned_name_(std::move(owned_name)) {}

    void unmap() noexcept;

    UniqueFd fd_;
    void* address_{nullptr};
    std::size_t size_{0};
    std::string owned_name_;
};

/// Cache line size on every CPU this project targets. Getting it wrong only
/// costs performance, never correctness.
inline constexpr std::size_t kCacheLine = 64;

/// The fixed part of the region. Producer and consumer each own one counter,
/// and the two sit on different cache lines: sharing one line would make every
/// push invalidate the consumer's copy and vice versa, which is false sharing
/// -- the classic way a lock-free structure ends up slower than a mutex.
struct alignas(kCacheLine) RingHeader {
    alignas(kCacheLine) std::atomic<std::uint64_t> tail{0};  // written by producer
    alignas(kCacheLine) std::atomic<std::uint64_t> head{0};  // written by consumer
    alignas(kCacheLine) std::uint32_t capacity{0};
    std::uint32_t element_size{0};
};

template <typename T>
class SharedRing {
    static_assert(std::is_trivially_copyable_v<T>,
                  "a shared-memory element is memcpy'd between address spaces, "
                  "so it must not contain pointers, references or heap handles");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "cross-process atomics must be lock-free");

public:
    /// Creates the region. Fails if it already exists, so two creators cannot
    /// silently share one ring and corrupt each other's indices.
    static std::optional<SharedRing> create(std::string_view name, std::uint32_t capacity) {
        if (capacity == 0U) {
            return std::nullopt;
        }
        auto mapping = SharedMapping::create(name, bytes_for(capacity));
        if (!mapping) {
            return std::nullopt;
        }
        // Placement new: the pages are zero-filled by the kernel, but the
        // atomics still need their lifetime to begin somewhere. This allocates
        // nothing and owns nothing -- the memory belongs to the mapping, which
        // is why the owning-memory check is wrong here.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* header = new (mapping->address()) RingHeader{};
        header->capacity = capacity;
        header->element_size = static_cast<std::uint32_t>(sizeof(T));
        return SharedRing{std::move(*mapping)};
    }

    /// Attaches to a region another process created.
    static std::optional<SharedRing> open(std::string_view name) {
        auto mapping = SharedMapping::open(name);
        if (!mapping || mapping->size() < sizeof(RingHeader)) {
            return std::nullopt;
        }
        SharedRing ring{std::move(*mapping)};
        // The peer's idea of T must match ours, or every read is garbage.
        if (ring.header().element_size != sizeof(T) || ring.header().capacity == 0U) {
            return std::nullopt;
        }
        return ring;
    }

    SharedRing(const SharedRing&) = delete;
    SharedRing& operator=(const SharedRing&) = delete;
    SharedRing(SharedRing&&) noexcept = default;
    SharedRing& operator=(SharedRing&&) noexcept = default;
    ~SharedRing() = default;

    /// Producer side. Returns false when the ring is full -- backpressure, the
    /// same contract BoundedQueue offers, for the same reason.
    bool try_push(const T& value) {
        RingHeader& hdr = header();
        // relaxed: this thread is the only writer of tail, so it already knows
        // the value; no ordering is needed to read back its own store.
        const std::uint64_t tail = hdr.tail.load(std::memory_order_relaxed);
        // acquire: pairs with the consumer's release store of head, so the
        // slot it freed is visible to us before we overwrite it.
        const std::uint64_t head = hdr.head.load(std::memory_order_acquire);

        if (tail - head >= hdr.capacity) {
            return false;
        }

        std::memcpy(slot(tail % hdr.capacity), &value, sizeof(T));
        // release: everything written above must be visible to the consumer
        // *before* it can observe the new tail. Without this the peer can read
        // a slot the compiler or CPU has not published yet.
        hdr.tail.store(tail + 1U, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false when the ring is empty.
    bool try_pop(T& out) {
        RingHeader& hdr = header();
        const std::uint64_t head = hdr.head.load(std::memory_order_relaxed);
        const std::uint64_t tail = hdr.tail.load(std::memory_order_acquire);

        if (head == tail) {
            return false;
        }

        std::memcpy(&out, slot(head % hdr.capacity), sizeof(T));
        hdr.head.store(head + 1U, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint32_t capacity() const { return header().capacity; }

    /// A snapshot, stale the moment it returns -- the same caveat as
    /// BoundedQueue::size(). Fine for logging, never for control flow.
    [[nodiscard]] std::uint64_t size() const {
        const RingHeader& hdr = header();
        return hdr.tail.load(std::memory_order_acquire) - hdr.head.load(std::memory_order_acquire);
    }

private:
    explicit SharedRing(SharedMapping mapping) : mapping_(std::move(mapping)) {}

    static std::size_t bytes_for(std::uint32_t capacity) {
        return sizeof(RingHeader) + (static_cast<std::size_t>(capacity) * sizeof(T));
    }

    RingHeader& header() { return *static_cast<RingHeader*>(mapping_.address()); }
    [[nodiscard]] const RingHeader& header() const {
        return *static_cast<const RingHeader*>(mapping_.address());
    }

    void* slot(std::uint64_t index) {
        auto* base = static_cast<std::uint8_t*>(mapping_.address()) + sizeof(RingHeader);
        return base + (index * sizeof(T));
    }

    SharedMapping mapping_;
};

}  // namespace av::ipc
