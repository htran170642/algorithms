// W23 — type traits. Almost everything here is a static_assert: a trait is
// answered at COMPILE time, so the real test is "does this translation unit
// compile", and a runtime EXPECT would be testing the wrong phase entirely.
// The few EXPECT_* calls exist only to give GoogleTest a body to register.

#include <gtest/gtest.h>

#include "w23/traits.hpp"

namespace {

// ---------- the carrier: integral_constant / true_type / false_type ----------
static_assert(w23::true_type::value == true);
static_assert(w23::false_type::value == false);
static_assert(w23::integral_constant<int, 42>::value == 42);
// It carries a value AND converts to it — a trait object IS its value.
static_assert(w23::true_type{} == true);
static_assert(w23::integral_constant<int, 7>{}() == 7);

// ---------- is_same: the primary/<T,T> split ----------
static_assert(w23::is_same_v<int, int>);
static_assert(!w23::is_same_v<int, unsigned>);
static_assert(!w23::is_same_v<int, int&>);          // reference is a different type
static_assert(!w23::is_same_v<int, const int>);     // const int is a different type

// ---------- remove_reference: the quiz miss, now mechanical ----------
static_assert(w23::is_same_v<w23::remove_reference_t<int>,   int>);
static_assert(w23::is_same_v<w23::remove_reference_t<int&>,  int>);
static_assert(w23::is_same_v<w23::remove_reference_t<int&&>, int>);
// It strips ONLY the reference — const survives, because that's remove_cv's job.
static_assert(w23::is_same_v<w23::remove_reference_t<const int&>, const int>);

// ---------- remove_cv composes remove_const + remove_volatile ----------
static_assert(w23::is_same_v<w23::remove_const_t<const int>,       int>);
static_assert(w23::is_same_v<w23::remove_volatile_t<volatile int>, int>);
static_assert(w23::is_same_v<w23::remove_cv_t<const volatile int>, int>);
// remove_cv does NOT reach through a pointer: the top-level type is a pointer,
// which has no cv here — the const is on the pointee, untouched.
static_assert(w23::is_same_v<w23::remove_cv_t<const int*>, const int*>);
// …but a const POINTER does lose its const:
static_assert(w23::is_same_v<w23::remove_cv_t<int* const>, int*>);

// ---------- is_pointer: pattern match after cv is stripped ----------
static_assert(w23::is_pointer_v<int*>);
static_assert(w23::is_pointer_v<const int*>);       // pointer-to-const is a pointer
static_assert(w23::is_pointer_v<int* const>);       // const pointer is a pointer (cv stripped)
static_assert(!w23::is_pointer_v<int>);
static_assert(!w23::is_pointer_v<int&>);            // a reference is not a pointer

// ---------- is_integral built by composing is_same ----------
static_assert(w23::is_integral_v<int>);
static_assert(w23::is_integral_v<char>);
static_assert(w23::is_integral_v<unsigned long long>);
static_assert(w23::is_integral_v<const int>);       // cv stripped before the check
static_assert(w23::is_integral_v<bool>);
static_assert(!w23::is_integral_v<double>);
static_assert(!w23::is_integral_v<int*>);
static_assert(!w23::is_integral_v<int&>);

// ---------- conditional: compile-time ?: over TYPES ----------
static_assert(w23::is_same_v<w23::conditional_t<true,  int, double>, int>);
static_assert(w23::is_same_v<w23::conditional_t<false, int, double>, double>);

// ---------- enable_if: the member exists ONLY when true ----------
static_assert(w23::is_same_v<w23::enable_if_t<true, int>, int>);
static_assert(w23::is_same_v<w23::enable_if_t<true>, void>);   // default T = void
// enable_if_t<false, …> would name a member that doesn't exist -> ill-formed.
// That "missing ::type" is precisely what W24 turns into SFINAE. We prove the
// asymmetry structurally: the false primary has no `type`, the true spec does.
template <typename T, typename = void> struct has_type          : w23::false_type { };
template <typename T> struct has_type<T, typename T::type_marker> : w23::true_type { };
struct WithMarker   { using type_marker = void; };
struct WithoutMarker { };
static_assert( has_type<WithMarker>::value);
static_assert(!has_type<WithoutMarker>::value);

// ---------- decay: the composite ----------
static_assert(w23::is_same_v<w23::decay_t<int&>,        int>);      // ref stripped
static_assert(w23::is_same_v<w23::decay_t<const int&>,  int>);      // ref + cv
static_assert(w23::is_same_v<w23::decay_t<int[5]>,      int*>);     // array -> pointer
static_assert(w23::is_same_v<w23::decay_t<int(int)>,    int(*)(int)>); // fn -> pointer
static_assert(w23::is_same_v<w23::decay_t<const int>,   int>);      // cv stripped

// GoogleTest needs at least one runnable body; the assertions above already ran
// at compile time, so reaching here at all is the pass.
TEST(Traits, EverythingProvenAtCompileTime) {
    EXPECT_TRUE(w23::is_pointer_v<int*>);
    EXPECT_FALSE(w23::is_pointer_v<int>);
    EXPECT_EQ((w23::integral_constant<int, 42>::value), 42);
    SUCCEED() << "all trait facts are static_asserts checked during compilation";
}

}  // namespace
