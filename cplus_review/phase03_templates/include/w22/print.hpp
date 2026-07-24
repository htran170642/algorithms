// W22 — variadic templates & fold expressions.
//
// Every function here exists to make one quiz answer executable:
//   printAllTo  — a fold needs an initial value to survive an empty pack
//   printTo     — peel the first element off the pack to fix the separator
//   format      — a comma fold is the only expansion with guaranteed order
//   Tuple       — where recursion still beats folding
#pragma once

#include <cstddef>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace w22 {

// ---------------------------------------------------------------- printAll --
// (os << ... << ts) is a BINARY LEFT fold:  ((os << t1) << t2) << t3
// The initial value is `os` itself, which is why the empty pack is fine — the
// whole expression collapses to `os` and nothing is written. The unary form
// (ts << ...) would be ill-formed for an empty pack, exactly like (ts + ...).
//
// The cast is not decoration. With an empty pack the fold really does collapse
// to the bare expression `os`, and GCC then rejects the line with
//   error: statement has no effect [-Werror=unused-value]
// — the compiler independently proving the collapse this comment claims.
template <typename... Ts>
void printAllTo(std::ostream& os, const Ts&... ts) {
    static_cast<void>((os << ... << ts));   // inner parens belong to the FOLD
}

template <typename... Ts>
void printAll(const Ts&... ts) {
    printAllTo(std::cout, ts...);
}

// ------------------------------------------------------------------- print --
// The separator problem: a space belongs BETWEEN elements, so the first element
// must behave differently from the rest. There is no runtime branch that can do
// that — so the first element is peeled out of the pack in the signature, and
// only `rest` is expanded. n elements => n-1 separators, by construction.
inline void printTo(std::ostream& os) {
    os << '\n';
}

template <typename First, typename... Rest>
void printTo(std::ostream& os, const First& first, const Rest&... rest) {
    os << first;
    ((os << ' ' << rest), ...);   // comma fold — see format() for why it matters
    os << '\n';
}

template <typename... Ts>
void print(const Ts&... ts) {
    printTo(std::cout, ts...);
}

// ------------------------------------------------------------------ format --
// Type-safe printf: no format specifiers, no varargs, no way to say %d and pass
// a string. Each argument is written by operator<<, chosen at compile time.
//
// The comma fold is load-bearing. `emit` mutates `pos`, so the calls MUST run
// left to right — and a comma fold guarantees that. Ordinary function arguments
// do not: the order of evaluation of f(a(), b(), c()) is unspecified, so
// expanding into an argument list here would be a real bug on some compilers.
template <typename... Ts>
[[nodiscard]] std::string format(std::string_view fmt, const Ts&... ts) {
    std::ostringstream out;
    std::size_t pos = 0;

    // [[maybe_unused]] for the same reason: format("plain") has an empty pack, so
    // (emit(ts), ...) expands to nothing and `emit` is never called. Second
    // diagnostic, same fact — an empty pack expands to literally no code.
    [[maybe_unused]] auto emit = [&](const auto& value) {
        const std::size_t next = fmt.find("{}", pos);
        if (next == std::string_view::npos) {
            return;                                   // more args than {} — drop
        }
        out << fmt.substr(pos, next - pos) << value;
        pos = next + 2;
    };

    (emit(ts), ...);
    out << fmt.substr(pos);                           // tail, plus any unfilled {}
    return out.str();
}

// ------------------------------------------------------------------- Tuple --
// A pack cannot be stored. `Ts... ts;` is not a member declaration, so a tuple
// has to turn the pack into a chain of nested types: head + tail. This is the
// case fold expressions do NOT cover — folding combines values into ONE value,
// while a tuple must keep every element separately addressable.
template <typename... Ts>
struct Tuple { };                                     // primary: the empty tail

template <typename Head, typename... Tail>
struct Tuple<Head, Tail...> {                         // partial specialization (W21 §3.5)
    Head           head;
    Tuple<Tail...> tail;
};

constexpr Tuple<> makeTuple() {
    return {};
}

template <typename Head, typename... Tail>
constexpr Tuple<Head, Tail...> makeTuple(Head head, Tail... tail) {
    return Tuple<Head, Tail...>{ head, makeTuple(tail...) };
}

template <std::size_t N, typename Head, typename... Tail>
constexpr auto& get(Tuple<Head, Tail...>& t) {
    if constexpr (N == 0) {
        return t.head;
    } else {
        return get<N - 1>(t.tail);
    }
}

template <std::size_t N, typename Head, typename... Tail>
constexpr const auto& get(const Tuple<Head, Tail...>& t) {
    if constexpr (N == 0) {
        return t.head;
    } else {
        return get<N - 1>(t.tail);
    }
}

// sizeof... COUNTS elements; sizeof MEASURES bytes. Same letters, unrelated jobs.
template <typename... Ts>
constexpr std::size_t size(const Tuple<Ts...>&) {
    return sizeof...(Ts);
}

}  // namespace w22
