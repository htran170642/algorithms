// W23 — a from-scratch subset of <type_traits>.
//
// Every trait here is one of two shapes, and the whole week is learning to tell
// them apart on sight:
//
//   VALUE trait  ("is T a …?")     -> carries a bool  -> read with ::value
//   TYPE  trait  ("turn T into …") -> carries a type  -> read with ::type
//
// The machinery underneath both is the SAME thing you built for Tuple in W22:
// a primary template stating the default, plus partial specializations that the
// compiler prefers when they match. There it stored head/tail; here it answers
// questions about a type. Same mechanism, different job.
//
// Naming mirrors std:: exactly (integral_constant, is_same, remove_reference…)
// so that reading <type_traits> afterwards feels like re-reading your own code.
#pragma once

#include <cstddef>   // std::size_t — only for array extents below

namespace w23 {

// ============================================================ the carrier ====
// integral_constant is the box every VALUE trait ships its answer in. It pins a
// compile-time constant `v` of type `T` to a distinct type. `true_type` and
// `false_type` are just the two bool boxes — and because they are *types*, a
// trait can INHERIT from them and get `value` for free instead of redeclaring
// it. That inheritance is the trick behind is_same, is_pointer, everything.
template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type       = integral_constant;                 // the trait's own type

    constexpr operator value_type() const noexcept { return value; }   // implicit -> v
    constexpr value_type operator()() const noexcept { return value; } // callable  -> v
};

template <bool B>
using bool_constant = integral_constant<bool, B>;

using true_type  = bool_constant<true>;
using false_type = bool_constant<false>;

// ================================================== VALUE traits (::value) ====
// is_same — the simplest possible value trait. Primary says "different"; the
// partial specialization <T, T> only matches when both args are the identical
// type, and it says "same". No object of T or U is ever created — this compares
// types, which is exactly why it can't be an ordinary bool function (Q4).
template <typename T, typename U>
struct is_same : false_type { };

template <typename T>
struct is_same<T, T> : true_type { };

template <typename T, typename U>
inline constexpr bool is_same_v = is_same<T, U>::value;

// is_const — a function type and a reference type are the only types that refuse
// `const`; decay/is_function below lean on that. Detected by pattern, like Tuple.
template <typename T> struct is_const          : false_type { };
template <typename T> struct is_const<const T> : true_type  { };
template <typename T> inline constexpr bool is_const_v = is_const<T>::value;

// is_lvalue_reference / is_rvalue_reference — `T&` and `T&&` are two different
// patterns, so they need two different specializations. A plain T matches neither.
template <typename T> struct is_lvalue_reference      : false_type { };
template <typename T> struct is_lvalue_reference<T&>  : true_type  { };
template <typename T> inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<T>::value;

template <typename T> struct is_rvalue_reference       : false_type { };
template <typename T> struct is_rvalue_reference<T&&>  : true_type  { };
template <typename T> inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<T>::value;

// =================================================== TYPE traits (::type) =====
// remove_reference — the one you got wrong in the quiz. It returns a TYPE, so it
// carries `using type`, never `static constexpr`. Three patterns: plain, T&, T&&.
template <typename T> struct remove_reference       { using type = T; };
template <typename T> struct remove_reference<T&>   { using type = T; };
template <typename T> struct remove_reference<T&&>  { using type = T; };
template <typename T> using remove_reference_t = typename remove_reference<T>::type;

// remove_const / remove_volatile / remove_cv — peel one qualifier per template,
// then compose. remove_cv doesn't need its own specialization: it just chains the
// other two. Composition, not repetition — the same instinct decay uses below.
template <typename T> struct remove_const             { using type = T; };
template <typename T> struct remove_const<const T>    { using type = T; };
template <typename T> using remove_const_t = typename remove_const<T>::type;

template <typename T> struct remove_volatile              { using type = T; };
template <typename T> struct remove_volatile<volatile T>  { using type = T; };
template <typename T> using remove_volatile_t = typename remove_volatile<T>::type;

template <typename T>
struct remove_cv { using type = remove_volatile_t<remove_const_t<T>>; };
template <typename T> using remove_cv_t = typename remove_cv<T>::type;

// is_pointer — the forward-link promised in W21 §3.5 and W22. Note it strips cv
// FIRST: `int* const` is still a pointer, so we ask the question about the bare
// type. A helper does the pattern-match; the public trait feeds it remove_cv_t.
template <typename T> struct is_pointer_helper      : false_type { };
template <typename T> struct is_pointer_helper<T*>  : true_type  { };

template <typename T>
struct is_pointer : is_pointer_helper<remove_cv_t<T>> { };
template <typename T> inline constexpr bool is_pointer_v = is_pointer<T>::value;

// is_integral — a value trait with no clever pattern to exploit, so it is built
// by COMPOSING is_same over the finite list of integral types (cv stripped once).
// This is exactly how you'd want to reuse a trait you already trust.
template <typename T>
struct is_integral : bool_constant<
    is_same_v<remove_cv_t<T>, bool>               ||
    is_same_v<remove_cv_t<T>, char>               ||
    is_same_v<remove_cv_t<T>, signed char>        ||
    is_same_v<remove_cv_t<T>, unsigned char>      ||
    is_same_v<remove_cv_t<T>, char8_t>            ||
    is_same_v<remove_cv_t<T>, char16_t>           ||
    is_same_v<remove_cv_t<T>, char32_t>           ||
    is_same_v<remove_cv_t<T>, wchar_t>            ||
    is_same_v<remove_cv_t<T>, short>              ||
    is_same_v<remove_cv_t<T>, unsigned short>     ||
    is_same_v<remove_cv_t<T>, int>                ||
    is_same_v<remove_cv_t<T>, unsigned int>       ||
    is_same_v<remove_cv_t<T>, long>               ||
    is_same_v<remove_cv_t<T>, unsigned long>      ||
    is_same_v<remove_cv_t<T>, long long>          ||
    is_same_v<remove_cv_t<T>, unsigned long long> > { };
template <typename T> inline constexpr bool is_integral_v = is_integral<T>::value;

// ======================================== the two that unlock W24 (SFINAE) ====
// conditional<B, T, F> — a compile-time ?:. Primary picks T; the <false, …>
// specialization picks F. One boolean chooses one of two TYPES.
template <bool B, typename T, typename F>
struct conditional { using type = T; };
template <typename T, typename F>
struct conditional<false, T, F> { using type = F; };
template <bool B, typename T, typename F>
using conditional_t = typename conditional<B, T, F>::type;

// enable_if<B, T> — the whole point is the ASYMMETRY: the primary has NO `type`
// member at all, and only the <true, …> specialization supplies one. Ask for
// `enable_if_t<false, X>` and you name a member that doesn't exist — a hard
// error here, but a *silent removal from the overload set* under SFINAE (W24).
template <bool B, typename T = void> struct enable_if { };            // no ::type
template <typename T> struct enable_if<true, T> { using type = T; };
template <bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// =========================================== the composite capstone: decay ====
// decay<T> models what happens to a type when passed BY VALUE to a function:
//   • references are stripped        (T& , T&&  -> T)
//   • arrays decay to pointers       (T[N]      -> T*)
//   • functions decay to pointers    (R(Args…)  -> R(*)(Args…))
//   • otherwise cv is stripped       (const T   -> T)
// It is nothing new — just remove_reference, is_array, remove_extent,
// is_function and add_pointer wired together with conditional_t. Composition is
// where all these small traits pay off.
template <typename T> struct is_array                  : false_type { };
template <typename T> struct is_array<T[]>             : true_type  { };
template <typename T, std::size_t N> struct is_array<T[N]> : true_type { };
template <typename T> inline constexpr bool is_array_v = is_array<T>::value;

template <typename T> struct remove_extent                  { using type = T; };
template <typename T> struct remove_extent<T[]>            { using type = T; };
template <typename T, std::size_t N> struct remove_extent<T[N]> { using type = T; };
template <typename T> using remove_extent_t = typename remove_extent<T>::type;

// A function type is the sole type that accepts neither `const` nor `&`.
template <typename T>
struct is_function : bool_constant<!is_const_v<const T> && !is_lvalue_reference_v<T>> { };
template <typename T> inline constexpr bool is_function_v = is_function<T>::value;

template <typename T>
struct add_pointer { using type = remove_reference_t<T>*; };
template <typename T> using add_pointer_t = typename add_pointer<T>::type;

template <typename T>
class decay {
    using U = remove_reference_t<T>;                       // step 1: drop the ref
public:
    using type = conditional_t<
        is_array_v<U>,    remove_extent_t<U>*,             // array    -> pointer
        conditional_t<
            is_function_v<U>, add_pointer_t<U>,            // function -> pointer
            remove_cv_t<U>>>;                              // else     -> strip cv
};
template <typename T> using decay_t = typename decay<T>::type;

}  // namespace w23
