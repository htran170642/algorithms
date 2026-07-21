#pragma once

// unique_ptr / shared_ptr / weak_ptr built by hand.
//
// The heart is the CONTROL BLOCK: a separate heap object holding two counts.
// strong_count reaches 0 -> the object is destroyed. weak_count reaches 0 (and
// strong is already 0) -> the control block itself is freed. Counts are atomic
// so copying/destroying a shared_ptr is thread-safe (the pointee is not).
//
// This is what every C++ programmer uses daily and rarely sees.

#include <atomic>
#include <cstddef>
#include <utility>

namespace cr {

// ============================================================ unique_ptr
template <typename T>
class UniquePtr {
public:
    UniquePtr() noexcept = default;
    explicit UniquePtr(T* p) noexcept : ptr_(p) {}

    UniquePtr(const UniquePtr&)            = delete;   // one owner
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    UniquePtr& operator=(UniquePtr&& o) noexcept {
        if (this != &o) { delete ptr_; ptr_ = std::exchange(o.ptr_, nullptr); }
        return *this;
    }

    ~UniquePtr() { delete ptr_; }

    T& operator*()  const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T* release() noexcept { return std::exchange(ptr_, nullptr); }
    void reset(T* p = nullptr) noexcept { delete ptr_; ptr_ = p; }

private:
    T* ptr_ = nullptr;
};

// ============================================================ control block
struct ControlBlock {
    std::atomic<long> strong{1};    // how many shared_ptr own the object
    std::atomic<long> weak{1};      // (# weak_ptr) + ONE collective ref held by the
                                    // strong side. That +1 keeps the control block
                                    // alive while the object's destructor runs — which
                                    // may itself drop the last weak_ptr. Without it,
                                    // delete ptr_ can free this block, and the code
                                    // right after would touch freed memory (a real UAF
                                    // that ASAN caught in W19).
};

template <typename T> class WeakPtr;

// ============================================================ shared_ptr
template <typename T>
class SharedPtr {
public:
    SharedPtr() noexcept = default;

    explicit SharedPtr(T* p) : ptr_(p), ctrl_(new ControlBlock{}) {}

    SharedPtr(const SharedPtr& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_relaxed);   // one more owner
    }

    SharedPtr(SharedPtr&& o) noexcept
        : ptr_(std::exchange(o.ptr_, nullptr)), ctrl_(std::exchange(o.ctrl_, nullptr)) {}

    SharedPtr& operator=(SharedPtr o) noexcept {   // copy-and-swap (W4)
        swap(*this, o);
        return *this;
    }

    ~SharedPtr() { release(); }

    friend void swap(SharedPtr& a, SharedPtr& b) noexcept {
        std::swap(a.ptr_, b.ptr_);
        std::swap(a.ctrl_, b.ctrl_);
    }

    T& operator*()  const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] long useCount() const noexcept {
        return ctrl_ ? ctrl_->strong.load(std::memory_order_relaxed) : 0;
    }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    friend class WeakPtr<T>;
    SharedPtr(T* p, ControlBlock* c) noexcept : ptr_(p), ctrl_(c) {}   // used by weak.lock()

    void release() noexcept {
        if (!ctrl_) return;
        if (ctrl_->strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete ptr_;                 // last owner -> destroy the object.
            // The collective weak ref (weak started at 1) kept ctrl_ alive through
            // that destructor. Now drop it; if it was the last weak, free the block.
            if (ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ctrl_;
            }
        }
    }

    T*            ptr_  = nullptr;
    ControlBlock* ctrl_ = nullptr;
};

template <typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
    return SharedPtr<T>(new T(std::forward<Args>(args)...));   // forwarding (W6)
}

// ============================================================ weak_ptr
template <typename T>
class WeakPtr {
public:
    WeakPtr() noexcept = default;

    WeakPtr(const SharedPtr<T>& s) noexcept : ptr_(s.ptr_), ctrl_(s.ctrl_) {   // NOLINT
        if (ctrl_) ctrl_->weak.fetch_add(1, std::memory_order_relaxed);        // observe, don't own
    }

    WeakPtr(const WeakPtr& o) noexcept : ptr_(o.ptr_), ctrl_(o.ctrl_) {
        if (ctrl_) ctrl_->weak.fetch_add(1, std::memory_order_relaxed);
    }

    ~WeakPtr() { release(); }

    WeakPtr& operator=(WeakPtr o) noexcept { swap(*this, o); return *this; }
    friend void swap(WeakPtr& a, WeakPtr& b) noexcept {
        std::swap(a.ptr_, b.ptr_); std::swap(a.ctrl_, b.ctrl_);
    }

    [[nodiscard]] bool expired() const noexcept {
        return !ctrl_ || ctrl_->strong.load(std::memory_order_acquire) == 0;
    }

    // lock(): get a shared_ptr IF the object is still alive, else empty.
    [[nodiscard]] SharedPtr<T> lock() const noexcept {
        if (!ctrl_) return {};
        long s = ctrl_->strong.load(std::memory_order_relaxed);
        while (s != 0) {                 // try to bump strong from s to s+1, atomically
            if (ctrl_->strong.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel)) {
                return SharedPtr<T>(ptr_, ctrl_);
            }
        }
        return {};                       // object already destroyed
    }

private:
    void release() noexcept {
        if (!ctrl_) return;
        // The strong side holds one collective weak ref until strong hits 0, so while
        // any shared_ptr is alive weak stays > 0. Reaching 0 here therefore means both
        // the object and the collective ref are already gone: free the block.
        if (ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete ctrl_;
        }
    }

    T*            ptr_  = nullptr;
    ControlBlock* ctrl_ = nullptr;
};

}  // namespace cr
