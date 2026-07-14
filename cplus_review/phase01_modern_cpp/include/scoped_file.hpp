#pragma once

// RAII over a C FILE*. The destructor is the whole point: the handle is
// released on scope exit, on early return, and on an exception thrown
// straight through this frame. You cannot forget to close it.
//
// Move-only, on purpose: a file handle has exactly ONE owner. Copying it
// would mean two objects both believing they must fclose() the same FILE*.

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace cr {

class ScopedFile {
public:
    ScopedFile(const std::string& path, const char* mode)
        : f_(std::fopen(path.c_str(), mode)) {
        if (f_ == nullptr) {
            throw std::runtime_error("cannot open " + path);
        }
    }

    // --- Rule of Five: move-only ---
    ScopedFile(const ScopedFile&)            = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    ScopedFile(ScopedFile&& other) noexcept
        : f_(std::exchange(other.f_, nullptr)) {}

    ScopedFile& operator=(ScopedFile&& other) noexcept {
        if (this != &other) {
            close();
            f_ = std::exchange(other.f_, nullptr);
        }
        return *this;
    }

    ~ScopedFile() { close(); }

    [[nodiscard]] bool valid() const noexcept { return f_ != nullptr; }

    void write(const std::string& s) {
        if (std::fputs(s.c_str(), f_) == EOF) {
            throw std::runtime_error("write failed");
        }
    }

private:
    // noexcept: a destructor must never throw. If it does during stack
    // unwinding, std::terminate() is called and your process dies.
    void close() noexcept {
        if (f_ != nullptr) {
            std::fclose(f_);
            f_ = nullptr;
        }
    }

    std::FILE* f_;
};

}  // namespace cr
