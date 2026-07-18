#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>

namespace {

// ============================================== constexpr: may run at compile time
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// consteval: MUST run at compile time
consteval int factorialForced(int n) {
    return factorial(n);
}

// ============================================== a compile-time lookup table
constexpr std::array<int, 10> makeSquares() {
    std::array<int, 10> a{};
    // std::size_t (unsigned) matches array's index type. Using `int` here trips
    // -Wsign-conversion: int -> size_type could turn a negative index into a
    // huge one. Week 0's warnings caught it.
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = static_cast<int>(i * i);
    }
    return a;
}

// ============================================== a concept + a constrained function
template <typename T>
concept Number = std::integral<T> || std::floating_point<T>;

template <Number T>
constexpr T addNumbers(T a, T b) {
    return a + b;
}

}  // namespace

// ================= 1. computed at compile time — proven by static_assert
TEST(W10Constexpr, FactorialIsAConstantExpression) {
    // static_assert can ONLY see values known at compile time. If this compiles,
    // factorial(5) really was evaluated by the compiler, not at runtime.
    static_assert(factorial(5) == 120);
    static_assert(factorialForced(5) == 120);

    EXPECT_EQ(factorial(5), 120);
}

TEST(W10Constexpr, ConstexprAlsoWorksAtRuntime) {
    int n = 5;                       // runtime value
    EXPECT_EQ(factorial(n), 120);    // same function, now runs at runtime — fine
}

// ================= 2. the lookup table is baked into the binary
TEST(W10Constexpr, LookupTableIsCompileTime) {
    constexpr auto squares = makeSquares();

    static_assert(squares[0] == 0);
    static_assert(squares[3] == 9);
    static_assert(squares[9] == 81);

    EXPECT_EQ(squares[7], 49);
}

// ================= 3. concepts constrain, and produce readable errors
TEST(W10Constexpr, ConceptAcceptsNumbersRejectsOthers) {
    static_assert(Number<int>);
    static_assert(Number<double>);
    static_assert(!Number<std::string_view>);   // string is NOT a Number

    EXPECT_EQ(addNumbers(3, 4), 7);
    EXPECT_DOUBLE_EQ(addNumbers(1.5, 2.5), 4.0);

    // addNumbers("a", "b");   // <-- error: constraints not satisfied — Number<const char*> false
}

// ================= 4. if constexpr — compile-time branch selection
template <typename T>
constexpr std::string_view kindOf() {
    if constexpr (std::is_integral_v<T>) {
        return "integral";           // the other branch is DISCARDED, not compiled
    } else if constexpr (std::is_floating_point_v<T>) {
        return "floating";
    } else {
        return "other";
    }
}

TEST(W10Constexpr, IfConstexprPicksABranchAtCompileTime) {
    static_assert(kindOf<int>()    == "integral");
    static_assert(kindOf<double>() == "floating");
    static_assert(kindOf<char*>()  == "other");
}
