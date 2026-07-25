// W24 — SFINAE / enable_if / concepts. The three describe_* functions must all
// agree; the concept machinery is verified with static_asserts (a concept is a
// compile-time predicate, so satisfaction is a compile-time fact) plus a few
// runtime EXPECTs where an actual value flows through.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include "w24/constraints.hpp"

namespace {

// ---------- the three eras give the same answer ----------
TEST(Eras, AllThreeAgreeOnIntegral) {
    EXPECT_EQ(w24::describe_enable_if(42),    "integral");
    EXPECT_EQ(w24::describe_if_constexpr(42), "integral");
    EXPECT_EQ(w24::describe_concept(42),      "integral");
}

TEST(Eras, AllThreeAgreeOnNonIntegral) {
    EXPECT_EQ(w24::describe_enable_if(3.14),    "not integral");
    EXPECT_EQ(w24::describe_if_constexpr(3.14), "not integral");
    EXPECT_EQ(w24::describe_concept(3.14),      "not integral");
}

// describe_concept picks the constrained overload for int by SUBSUMPTION — the
// fallback is unconstrained, yet the more-constrained one wins. enable_if needs
// hand-written mutual exclusion to achieve the same; concepts do it for free.
TEST(Concepts, SubsumptionPrefersTheMoreConstrainedOverload) {
    EXPECT_EQ(w24::describe_concept('a'), "integral");   // char is integral
    EXPECT_EQ(w24::describe_concept(1L),  "integral");   // long too
}

// ---------- concepts are compile-time predicates ----------
static_assert(w24::Integral<int>);
static_assert(w24::Integral<unsigned long long>);
static_assert(!w24::Integral<double>);
static_assert(!w24::Integral<int*>);

// Printable is a USAGE requirement: it asks whether `os << t` compiles.
struct NoStream { };                          // deliberately has no operator<<
static_assert(w24::Printable<int>);
static_assert(w24::Printable<std::string>);
static_assert(w24::Printable<const char*>);
static_assert(!w24::Printable<NoStream>);     // rejected: the << expression is invalid

// A requires-expression is only a SOFT detector inside a DEPENDENT context. A
// standalone `requires(NoStream n){ to_string(n); }` at namespace scope is
// evaluated EAGERLY — an invalid call there is a hard error, not `false`. So
// every "is this call valid?" probe is a variable TEMPLATE, made false-able by
// the very substitution boundary that defines SFINAE. (Confirmed against GCC 13.)
template <typename T> constexpr bool to_string_ok = requires(const T& t) { w24::to_string(t); };
template <typename T> constexpr bool id_a_ok = requires(T v) { w24::id_a(v); };
template <typename T> constexpr bool id_b_ok = requires(T v) { w24::id_b(v); };
template <typename T> constexpr bool id_c_ok = requires(T v) { w24::id_c(v); };
template <typename T> constexpr bool id_d_ok = requires(T v) { w24::id_d(v); };

TEST(Concepts, PrintableGatesToString) {
    EXPECT_EQ(w24::to_string(42),                 "42");
    EXPECT_EQ(w24::to_string(std::string("hi")),  "hi");
    EXPECT_EQ(w24::to_string(2.5),                "2.5");
    // to_string(NoStream{}) would NOT compile — "constraint not satisfied:
    // Printable<NoStream>". Proven via the templated detector, no hard error:
    static_assert( to_string_ok<int>);
    static_assert(!to_string_ok<NoStream>);
    SUCCEED();
}

// ---------- four syntaxes, one constraint: all accept int, all reject double ----
TEST(FourSyntaxes, AllAcceptIntegral) {
    EXPECT_EQ(w24::id_a(5), 5);
    EXPECT_EQ(w24::id_b(5), 5);
    EXPECT_EQ(w24::id_c(5), 5);
    EXPECT_EQ(w24::id_d(5), 5);
}

// Rejection proven through the templated detectors: integral accepted, double
// rejected — no hard error, because each probe lives in a dependent context.
static_assert( id_a_ok<int> && !id_a_ok<double>);
static_assert( id_b_ok<int> && !id_b_ok<double>);
static_assert( id_c_ok<int> && !id_c_ok<double>);
static_assert( id_d_ok<int> && !id_d_ok<double>);

// ---------- the immediate-context boundary (quiz Q3) ----------
// A substitution failure in the SIGNATURE is SFINAE (soft): this trait detects
// "does T have a member type ::type?" without ever hard-erroring on int.
template <typename T, typename = void>
struct has_member_type : std::false_type { };
template <typename T>
struct has_member_type<T, std::void_t<typename T::type>> : std::true_type { };

struct WithType    { using type = int; };
static_assert( has_member_type<WithType>::value);
static_assert(!has_member_type<int>::value);   // `int::type` in immediate context -> soft
// The same `typename T::type` inside a function BODY would be a HARD error and
// no detection idiom could catch it — that is the whole point of "immediate
// context", and the reason enable_if conditions live in the signature.

}  // namespace
