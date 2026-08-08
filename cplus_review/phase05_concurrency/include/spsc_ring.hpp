#pragma once

// W38 — SPSC lock-free ring buffer. A REAL component (portfolio), not throwaway glue.
//
// Single Producer, Single Consumer. Exactly one thread ever calls push(), exactly
// one (other) thread ever calls pop(). That single fact is what lets this be
// lock-free WITHOUT any CAS/RMW:
//
//   * tail_ (write index) has ONE writer — the producer. Consumer only reads it.
//   * head_ (read index)  has ONE writer — the consumer. Producer only reads it.
//
// One writer per index ⇒ a plain store() is enough; we never "read-modify-write to
// beat another writer", so no compare_exchange, no fetch_add. But the indices are
// still std::atomic: each is read by the OTHER thread, and a plain int shared by a
// writer and a reader is a data race (UB) — the compiler could cache it in a
// register and the consumer would spin forever. Atomic solves visibility/tearing;
// CAS solves multiple-writer contention. SPSC drops the CAS, never the atomic.
//
// Ordering (the whole game):
//   push: construct payload  THEN  tail_.store(release)   -- release publishes it
//   pop : head_ needs no sync for the payload it reads, but tail_.load(acquire)
//         pairs with the producer's release so we SEE the constructed payload.
//   The reverse edge (freeing a slot) uses head_.store(release) / head_.load(acquire)
//   so the producer never overwrites a slot the consumer hasn't finished reading.
//
// Indices are MONOTONIC (never wrap): size == tail_ - head_, and we mask only when
// indexing storage. So empty (head==tail) and full (tail-head==Capacity) are never
// ambiguous, and every one of the Capacity slots is usable.
//
// head_ and tail_ live on separate cache lines (see kCacheLine): the producer hammers
// tail_, the consumer hammers head_; sharing a line would ping-pong it between cores
// on every operation (false sharing — cf. W31).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace cr {

// Own constant instead of std::hardware_destructive_interference_size: GCC warns
// (ABI-unstable) under -Werror. 64 B is the line size on every x86-64 we target.
inline constexpr std::size_t kCacheLine = 64;

template <class T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two (mask, not modulo)");

public:
    SpscRing() = default;

    ~SpscRing() {
        // Runs single-threaded after both worker threads have joined. Destroy every
        // element still in flight; the untouched slots were never constructed.
        for (std::uint64_t i = head_.load(std::memory_order_relaxed);
             i != tail_.load(std::memory_order_relaxed); ++i) {
            slot(i)->~T();
        }
    }

    // Contains atomics + raw storage: neither copyable nor movable. Rule of five,
    // explicit about it.
    SpscRing(const SpscRing&)            = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&)                 = delete;
    SpscRing& operator=(SpscRing&&)      = delete;

    // Producer side. Returns false if full (never blocks, never allocates).
    [[nodiscard]] bool push(const T& item) { return emplace(item); }
    [[nodiscard]] bool push(T&& item)      { return emplace(std::move(item)); }

    template <class... Args>
    [[nodiscard]] bool emplace(Args&&... args) {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);   // we own tail_
        // acquire pairs with the consumer's head_.store(release): we must SEE the slot
        // freed before we reuse it, so we don't clobber a value still being read.
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) {
            return false;   // full
        }
        ::new (static_cast<void*>(slot(tail))) T(std::forward<Args>(args)...);
        // release: everything above (the constructed payload) is visible to any
        // consumer that acquire-loads this new tail.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if empty. Moves the element into `out`.
    [[nodiscard]] bool pop(T& out) {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);   // we own head_
        // acquire pairs with the producer's tail_.store(release): see the payload.
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;   // empty
        }
        T* cell = slot(head);
        out = std::move(*cell);
        cell->~T();
        // release: the slot is now free; the producer's acquire-load of head_ sees it.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Snapshot size. Safe to call from either thread, but only an instant-in-time hint
    // (the other thread may move it immediately). Relaxed: it's advisory, not a fence.
    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(tail_.load(std::memory_order_relaxed) -
                                        head_.load(std::memory_order_relaxed));
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::uint64_t kMask = Capacity - 1;

    T* slot(std::uint64_t i) noexcept {
        return std::launder(reinterpret_cast<T*>(&storage_[(i & kMask) * sizeof(T)]));
    }

    // Raw, uninitialized storage; T is constructed in place only on push. aligned as T,
    // sized for Capacity elements. (std::aligned_storage is deprecated in C++23.)
    alignas(T) std::byte storage_[Capacity * sizeof(T)];

    // Each index on its own cache line so producer/consumer never fight over one line.
    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};   // consumer writes, producer reads
    alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0};   // producer writes, consumer reads
};

}  // namespace cr
