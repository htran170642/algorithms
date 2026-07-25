// W24 — SFINAE -> enable_if -> Concepts, the same constraint across four eras.
//
// The whole file answers one question: "how do I say 'this function only accepts
// integral types' — and make the compiler enforce it?" Watch the syntax shrink
// and the error messages get readable as we walk from C++11 to C++20.
//
//   Era 2  enable_if (C++11)   condition hidden in the signature; brutal errors
//   Era 3  if constexpr (C++17) one function, a compile-time branch — but NO
//                               control over the overload set
//   Era 4  concepts (C++20)    the condition said out loud, subsumption picks
//                               the overload, errors name the failed requirement
//
// (Era 1, tag dispatch, is left in the note — it's the museum piece.)
#pragma once

#include <concepts>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace w24 {

// =============================================== Era 2 — enable_if (C++11) ====
// Two overloads. The condition lives in the RETURN TYPE, in the immediate
// context, so a substitution failure removes the overload instead of erroring
// (SFINAE). The catch: the two conditions MUST be mutually exclusive by hand —
// `is_integral_v<T>` and `!is_integral_v<T>` — or the overloads clash. Miss the
// `!` and you get a redefinition/ambiguity, not a helpful message.
template <typename T>
std::enable_if_t<std::is_integral_v<T>, std::string>
describe_enable_if(T) {
    return "integral";
}

template <typename T>
std::enable_if_t<!std::is_integral_v<T>, std::string>
describe_enable_if(T) {
    return "not integral";
}

// ============================================= Era 3 — if constexpr (C++17) ==
// ONE function, a branch chosen at compile time. Cleaner than enable_if — but a
// different tool: it cannot remove itself from an overload set, cannot express
// "I don't accept this type." It picks a branch; it does not gate the function.
template <typename T>
std::string describe_if_constexpr(T) {
    if constexpr (std::is_integral_v<T>) {
        return "integral";
    } else {
        return "not integral";
    }
}

// ================================================= Era 4 — concepts (C++20) ==
// A concept is a named, compile-time predicate over types. `Integral` just wraps
// the trait we built last week; std::integral already exists — we spell it out
// to see the machinery.
template <typename T>
concept Integral = std::is_integral_v<T>;

// A concept built from a requires-expression: "T is Printable if `os << t` is
// valid syntax and yields an ostream&." This is the thing enable_if struggles to
// say — a *usage* requirement, not just a trait check.
template <typename T>
concept Printable = requires(std::ostream& os, const T& t) {
    { os << t } -> std::same_as<std::ostream&>;
};

// The payoff enable_if cannot match: the fallback is a PLAIN unconstrained
// template. For an integral T both are viable, but `Integral T` is *more
// constrained*, so subsumption picks it — no hand-written `!Integral`, no
// mutual-exclusion bookkeeping. Add a third overload for floats and subsumption
// still sorts them out.
template <typename T>
std::string describe_concept(T) {
    return "not integral";
}

template <Integral T>
std::string describe_concept(T) {
    return "integral";
}

// to_string gated on Printable: a type with no operator<< is rejected at the
// call site with "constraint not satisfied: Printable<T>", not a 200-line dump
// from somewhere deep inside a stream template.
template <Printable T>
std::string to_string(const T& value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

// ------------------------------- four syntaxes, one identical constraint ------
// All four say "T must be Integral." They are equivalent; pick by taste and
// terseness. This is the single most-tested "do you actually know C++20" fact.
template <Integral T>                              // (a) constrained type parameter
T id_a(T v) { return v; }

template <typename T> requires Integral<T>         // (b) requires-clause up front
T id_b(T v) { return v; }

template <typename T>
T id_c(T v) requires Integral<T> { return v; }     // (c) trailing requires-clause

Integral auto id_d(Integral auto v) { return v; }  // (d) abbreviated / constrained auto

}  // namespace w24
