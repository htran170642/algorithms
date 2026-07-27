#pragma once

// A teaching String. Two things to see with your own eyes:
//   1. SSO: short strings live INSIDE the object, zero heap allocation.
//   2. copy-and-swap: operator= is strong-exception-safe and
//      self-assignment-safe, for free, in three lines.
//
// Thrown away after this week. You use std::string forever after.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace cr {

class String {
    static constexpr std::size_t kBufSize = 16;   // inline capacity (incl. '\0')

public:
    // A global counter so the tests can PROVE when the heap is touched.
    static inline int heap_allocs = 0;
    static void resetAllocs() noexcept { heap_allocs = 0; }

    String() noexcept : size_(0) {
        buf_[0] = '\0';
        data_ = buf_;                 // points at its OWN inline buffer
    }

    String(const char* s) {           // NOLINT: implicit on purpose (like std::string)
        size_ = std::strlen(s);
        if (size_ < kBufSize) {
            data_ = buf_;             // short -> stay inline, NO allocation
        } else {
            data_ = allocate(size_ + 1);
        }
        std::memcpy(data_, s, size_ + 1);
    }

    String(const String& other) {
        size_ = other.size_;
        if (isShort()) {
            data_ = buf_;
        } else {
            data_ = allocate(size_ + 1);
        }
        std::memcpy(data_, other.data_, size_ + 1);
    }

    String(String&& other) noexcept {
        moveFrom(other);
    }

    // ONE assignment operator, by value. Handles copy AND move, is
    // strong-exception-safe and self-assignment-safe. This is the whole point.
    String& operator=(String other) noexcept {
        swap(*this, other);
        return *this;
    }

    ~String() {
        if (!isShort()) {
            delete[] data_;
        }
    }

    friend void swap(String& a, String& b) noexcept {
        // Both short, both long, or mixed — handle by normalizing through temps.
        // Simplest correct version: swap the raw bytes, then re-point any
        // data_ that was aiming at its own inline buffer.
        using std::swap;
        char tmp[kBufSize];
        std::memcpy(tmp, a.buf_, kBufSize);
        std::memcpy(a.buf_, b.buf_, kBufSize);
        std::memcpy(b.buf_, tmp, kBufSize);

        swap(a.size_, b.size_);

        char* adata = a.isShort() ? a.buf_ : b.data_;
        char* bdata = b.isShort() ? b.buf_ : a.data_;
        a.data_ = adata;
        b.data_ = bdata;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const char* c_str() const noexcept { return data_; }
    [[nodiscard]] bool isShort() const noexcept { return size_ < kBufSize; }

private:
    static char* allocate(std::size_t n) {
        ++heap_allocs;
        return new char[n];
    }

    void moveFrom(String& other) noexcept {
        size_ = other.size_;
        if (other.isShort()) {
            std::memcpy(buf_, other.buf_, kBufSize);
            data_ = buf_;
        } else {
            data_ = other.data_;      // steal the heap pointer
            other.data_ = other.buf_; // leave source valid & short
        }
        other.size_ = 0;
        other.buf_[0] = '\0';
    }

    char*       data_;             // -> buf_ (short) or heap (long)
    std::size_t size_;
    // Zero-initialized so the WHOLE 16 bytes are always defined. swap() below
    // blind-copies all kBufSize bytes for simplicity; without this, the bytes
    // past the string length would be uninitialized and that memcpy would read
    // indeterminate values — a real UB smell that GCC -O2 catches as
    // -Werror=uninitialized. Defining the tail costs one 16-byte clear per
    // construction and makes the copy correct.
    char        buf_[kBufSize]{};  // the inline SSO buffer
};

}  // namespace cr
