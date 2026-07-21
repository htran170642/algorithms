#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>
#include <version>          // defines __cpp_lib_expected when std::expected exists

// std::expected is C++23. __cpp_lib_expected is the standard feature-test macro:
// it respects the actual -std flag per translation unit, unlike a CMake header
// check that runs once at configure time. Present under -std=c++23, absent under
// -std=c++20 — so the dual-standard matrix builds both cleanly.
#if __cpp_lib_expected >= 202202L
#include <expected>
#endif

namespace {

// ---- optional: "maybe a value" -------------------------------------------
std::optional<int> firstEven(const std::vector<int>& v) {
    for (int x : v) {
        if (x % 2 == 0) return x;      // found -> wrapped value
    }
    return std::nullopt;               // none
}

// ---- variant: "one of several types" -------------------------------------
using Json = std::variant<std::nullptr_t, bool, int, std::string>;

std::string describe(const Json& j) {
    return std::visit([](auto&& x) -> std::string {          // dispatch on active type
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
        else if constexpr (std::is_same_v<T, bool>)      return x ? "true" : "false";
        else if constexpr (std::is_same_v<T, int>)       return "int";
        else                                             return "string";
    }, j);
}

#if __cpp_lib_expected >= 202202L
// ---- expected: "value OR error with reason" (C++23) ----------------------
std::expected<int, std::string> parseAge(const std::string& s) {
    if (s.empty()) return std::unexpected("empty input");
    for (char c : s) {
        if (c < '0' || c > '9') return std::unexpected("not a number: " + s);
    }
    int age = std::stoi(s);
    if (age > 150) return std::unexpected("implausible age");
    return age;                        // success -> value
}
#endif

}  // namespace

// ================= optional
TEST(W17Error, OptionalDistinguishesNoneFromValue) {
    EXPECT_EQ(firstEven({1, 3, 4, 5}), 4);
    EXPECT_EQ(firstEven({1, 3, 5}), std::nullopt);

    auto r = firstEven({2, 4});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 2);
    EXPECT_EQ(r.value_or(-1), 2);           // value_or: default if empty
    EXPECT_EQ(firstEven({1}).value_or(-1), -1);
}

// ================= variant
TEST(W17Error, VariantHoldsOneTypeAtATime) {
    Json j = 42;
    EXPECT_EQ(describe(j), "int");
    EXPECT_EQ(std::get<int>(j), 42);        // extract, throws if wrong type
    EXPECT_EQ(j.index(), 2u);               // which alternative (0-based)

    j = std::string("hi");
    EXPECT_EQ(describe(j), "string");
    EXPECT_TRUE(std::holds_alternative<std::string>(j));

    j = nullptr;
    EXPECT_EQ(describe(j), "null");
}

TEST(W17Error, GetWrongTypeThrows) {
    Json j = 42;
    EXPECT_THROW(std::get<std::string>(j), std::bad_variant_access);
}

// ================= expected (C++23 only)
#if __cpp_lib_expected >= 202202L
TEST(W17Error, ExpectedCarriesValueOrError) {
    auto ok = parseAge("42");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(*ok, 42);

    auto bad = parseAge("abc");
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error(), "not a number: abc");   // the REASON, not just failure

    EXPECT_FALSE(parseAge("").has_value());
    EXPECT_EQ(parseAge("999").error(), "implausible age");

    // value_or works like optional
    EXPECT_EQ(parseAge("abc").value_or(-1), -1);
}
#endif
