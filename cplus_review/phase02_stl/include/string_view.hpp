#pragma once

// A non-owning view into a character sequence: just {pointer, length}.
// It copies no data and allocates nothing — which is why it's fast, and why
// it dangles the instant the underlying data outlives it (W9 returns).
//
// This is std::string_view in miniature.

#include <cstddef>
#include <cstring>
#include <ostream>
#include <string>

namespace cr {

class StringView {
public:
    constexpr StringView() noexcept = default;

    // Implicit from const char* (like std::string_view) — a literal is a view.
    constexpr StringView(const char* s) : data_(s), size_(cLen(s)) {}   // NOLINT

    constexpr StringView(const char* s, std::size_t n) : data_(s), size_(n) {}

    // From std::string — views its buffer, copies nothing.
    StringView(const std::string& s) : data_(s.data()), size_(s.size()) {}  // NOLINT

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr const char* data() const noexcept { return data_; }

    constexpr char operator[](std::size_t i) const { return data_[i]; }

    [[nodiscard]] constexpr char front() const { return data_[0]; }
    [[nodiscard]] constexpr char back() const { return data_[size_ - 1]; }

    // substr returns another VIEW into the SAME buffer — no allocation.
    [[nodiscard]] constexpr StringView substr(std::size_t pos, std::size_t n) const {
        return StringView(data_ + pos, n);
    }

    [[nodiscard]] constexpr bool startsWith(StringView prefix) const {
        if (prefix.size_ > size_) return false;
        for (std::size_t i = 0; i < prefix.size_; ++i) {
            if (data_[i] != prefix.data_[i]) return false;
        }
        return true;
    }

    constexpr bool operator==(StringView other) const {
        if (size_ != other.size_) return false;
        for (std::size_t i = 0; i < size_; ++i) {
            if (data_[i] != other.data_[i]) return false;
        }
        return true;
    }

private:
    static constexpr std::size_t cLen(const char* s) {
        std::size_t n = 0;
        while (s[n] != '\0') ++n;
        return n;
    }

    const char* data_ = nullptr;
    std::size_t size_ = 0;
};

inline std::ostream& operator<<(std::ostream& os, StringView sv) {
    return os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
}

}  // namespace cr
