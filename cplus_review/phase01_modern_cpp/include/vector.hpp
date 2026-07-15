#pragma once

// A teaching Vector. Three things to see with your own eyes:
//   1. geometric growth  -> ~log2(n) reallocations, not n. Amortized O(1).
//   2. placement new      -> allocation is separated from construction.
//   3. move_if_noexcept    -> the reason W1 made you measure noexcept. If T's
//      move can throw, realloc COPIES to keep the strong guarantee.
//
// Thrown away after this week. You use std::vector forever after.

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cr {

template <typename T>
class Vector {
public:
    // Counters so the tests can PROVE growth behaviour and move-vs-copy.
    static inline int reallocs = 0;
    static void resetStats() noexcept { reallocs = 0; }

    Vector() noexcept = default;

    ~Vector() {
        clear();                       // destroy each live element
        ::operator delete(data_);      // then free the raw buffer
    }

    Vector(const Vector& other) {
        reserve(other.size_);
        for (std::size_t i = 0; i < other.size_; ++i) {
            construct(i, other.data_[i]);     // copy-construct each
        }
        size_ = other.size_;
    }

    Vector(Vector&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)),
          cap_(std::exchange(other.cap_, 0)) {}

    Vector& operator=(Vector other) noexcept {   // copy-and-swap (W4)
        swap(*this, other);
        return *this;
    }

    friend void swap(Vector& a, Vector& b) noexcept {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.cap_,  b.cap_);
    }

    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value)      { emplace_back(std::move(value)); }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        if (size_ == cap_) {
            grow();                                  // geometric
        }
        construct(size_, std::forward<Args>(args)...);
        return data_[size_++];
    }

    void reserve(std::size_t n) {
        if (n <= cap_) {
            return;
        }
        T* new_data = static_cast<T*>(::operator new(n * sizeof(T)));

        // THE HEART OF THE WEEK: move the existing elements over, but only if
        // T's move is noexcept. Otherwise copy, so a throw mid-transfer leaves
        // the old buffer intact and the strong guarantee holds.
        std::size_t i = 0;
        try {
            for (; i < size_; ++i) {
                ::new (&new_data[i]) T(std::move_if_noexcept(data_[i]));
            }
        } catch (...) {
            for (std::size_t j = 0; j < i; ++j) {
                new_data[j].~T();
            }
            ::operator delete(new_data);
            throw;                                    // old buffer untouched
        }

        for (std::size_t k = 0; k < size_; ++k) {
            data_[k].~T();
        }
        ::operator delete(data_);
        data_ = new_data;
        cap_  = n;
    }

    void clear() noexcept {
        for (std::size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }

    [[nodiscard]] T& operator[](std::size_t i) noexcept { return data_[i]; }
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    [[nodiscard]] T& at(std::size_t i) {
        if (i >= size_) {
            throw std::out_of_range("Vector::at");
        }
        return data_[i];
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return cap_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
    void grow() {
        ++reallocs;
        reserve(cap_ == 0 ? 1 : cap_ * 2);        // double
    }

    template <typename... Args>
    void construct(std::size_t i, Args&&... args) {
        ::new (&data_[i]) T(std::forward<Args>(args)...);   // placement new
    }

    T*          data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_  = 0;
};

}  // namespace cr
