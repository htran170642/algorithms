#pragma once

// Perfect forwarding, built by hand.
//
// The problem: a wrapper that passes arguments to another function must not
// change what they ARE. lvalue in -> lvalue out. rvalue in -> rvalue out.
// Otherwise you either copy what could have moved, or move what the caller
// still needs. Writing overloads instead needs 2^n of them for n parameters.

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace cr {

// ---- our own std::move: an UNCONDITIONAL cast to rvalue -------------------
template <typename T>
constexpr std::remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<std::remove_reference_t<T>&&>(t);
}

// ---- our own std::forward: a CONDITIONAL cast ------------------------------
// T carries the answer. If the caller passed an lvalue, T was deduced as U&,
// and U& && collapses back to U& — an lvalue. If they passed an rvalue, T is
// U, and this casts to U&&.
template <typename T>
constexpr T&& forward(std::remove_reference_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}

template <typename T>
constexpr T&& forward(std::remove_reference_t<T>&& t) noexcept {
    static_assert(!std::is_lvalue_reference_v<T>,
                  "cannot forward an rvalue as an lvalue");
    return static_cast<T&&>(t);
}

// ---- our own make_unique ---------------------------------------------------
// The textbook use of forwarding: pass ctor args through untouched.
template <typename T, typename... Args>
std::unique_ptr<T> makeUnique(Args&&... args) {
    return std::unique_ptr<T>(new T(cr::forward<Args>(args)...));
}

// ---- reference collapsing, made visible ------------------------------------
// Ask the compiler what T was deduced as, and assert on it in the tests.
enum class Deduced { Lvalue, Rvalue };

template <typename T>
constexpr Deduced whatWasDeduced(T&&) noexcept {
    if constexpr (std::is_lvalue_reference_v<T>) {
        return Deduced::Lvalue;      // T = U&  -> caller passed an lvalue
    } else {
        return Deduced::Rvalue;      // T = U   -> caller passed an rvalue
    }
}

}  // namespace cr
