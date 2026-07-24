// W22 — parameter packs, pack expansion, fold expressions.
// Every assertion here was a quiz answer first. Predict, then verify.

#include <gtest/gtest.h>

#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

#include "w22/print.hpp"

namespace {

// ---------- the expansion rule itself ----------
// Cover the "..." and read left: whatever remains IS the pattern, and it gets
// photocopied once per element. The pattern is never cut in half.
template <typename... Ts>
std::vector<int> doubled(Ts... ts) {
    return { (ts * 2)... };          // pattern = "(ts * 2)"  ->  2, 4, 6
}

template <typename... Ts>
std::vector<int> thenNinetyNine(Ts... ts) {
    return { ts..., 99 };            // pattern = "ts"; the 99 sits AFTER the ...
}

TEST(Expansion, PatternIsEverythingLeftOfTheDots) {
    EXPECT_EQ(doubled(1, 2, 3), (std::vector<int>{2, 4, 6}));
}

TEST(Expansion, LiteralAfterTheDotsIsNotPhotocopied) {
    EXPECT_EQ(thenNinetyNine(1, 2, 3), (std::vector<int>{1, 2, 3, 99}));
}

// ---------- sizeof... counts, sizeof measures ----------
template <typename... Ts>
constexpr std::size_t countOf(const Ts&...) {
    return sizeof...(Ts);
}

TEST(SizeofPack, CountsElementsAndNotBytes) {
    static_assert(countOf(1, 2.5, "hi") == 3);
    EXPECT_EQ(countOf(1, 2.5, "hi"), 3U);
    EXPECT_EQ(countOf(), 0U);

    // The number the name tempts you into: 4 + 8 + 8 = 20, nothing to do with it.
    EXPECT_NE(countOf(1, 2.5, "hi"),
              sizeof(int) + sizeof(double) + sizeof(const char*));
}

// ---------- folds ----------
TEST(Fold, BinaryLeftFoldSurvivesTheEmptyPack) {
    std::ostringstream out;
    w22::printAllTo(out);                       // (os << ... << ts)  collapses to os
    EXPECT_EQ(out.str(), "");
}

TEST(Fold, BinaryLeftFoldChainsTheStream) {
    std::ostringstream out;
    w22::printAllTo(out, 1, 2.5, "hi");
    EXPECT_EQ(out.str(), "12.5hi");
}

// A comma fold guarantees left-to-right evaluation. Expanding into a function
// argument list does NOT — argument evaluation order is unspecified — which is
// why format() below folds instead of expanding into a call.
template <typename... Ts>
std::string appendEach(Ts... ts) {
    std::string s;
    ((s += ts), ...);
    return s;
}

TEST(Fold, CommaFoldEvaluatesLeftToRight) {
    EXPECT_EQ(appendEach('a', 'b', 'c'), "abc");
}

// ---------- print: the separator problem ----------
TEST(Print, SeparatorSitsBetweenElementsNotBeforeThem) {
    std::ostringstream out;
    w22::printTo(out, 1, 2.5, "hi");
    EXPECT_EQ(out.str(), "1 2.5 hi\n");          // no leading and no trailing space
}

TEST(Print, OneElementGetsNoSeparatorAtAll) {
    std::ostringstream out;
    w22::printTo(out, 42);
    EXPECT_EQ(out.str(), "42\n");
}

TEST(Print, EmptyPackSelectsThePlainOverload) {
    std::ostringstream out;
    w22::printTo(out);                           // the template needs a First; only
    EXPECT_EQ(out.str(), "\n");                  // the non-template is viable (W21)
}

// ---------- format: type-safe printf ----------
TEST(Format, SubstitutesPlaceholdersInOrder) {
    EXPECT_EQ(w22::format("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
}

TEST(Format, MixedTypesNeedNoFormatSpecifiers) {
    // printf("%d", "oops") is a runtime crash; there is no %d to get wrong here.
    EXPECT_EQ(w22::format("{}:{}", "x", 2.5), "x:2.5");
}

TEST(Format, NoArgumentsIsJustTheLiteral) {
    EXPECT_EQ(w22::format("plain"), "plain");
}

TEST(Format, TooFewArgumentsLeaveThePlaceholderVisible) {
    EXPECT_EQ(w22::format("{} {}", 1), "1 {}");
}

TEST(Format, TooManyArgumentsAreDropped) {
    EXPECT_EQ(w22::format("{}", 1, 2), "1");
}

// ---------- Tuple: where folding cannot help ----------
TEST(Tuple, GetReachesEachElementByIndex) {
    auto t = w22::makeTuple(1, 2.5, std::string("hi"));
    EXPECT_EQ(w22::get<0>(t), 1);
    EXPECT_DOUBLE_EQ(w22::get<1>(t), 2.5);
    EXPECT_EQ(w22::get<2>(t), "hi");
}

TEST(Tuple, SizeIsKnownAtCompileTime) {
    constexpr auto t = w22::makeTuple(1, 2, 3);
    static_assert(w22::size(t) == 3);
    static_assert(w22::get<1>(t) == 2);
    EXPECT_EQ(w22::size(t), 3U);
}

TEST(Tuple, EmptyTupleHasSizeZero) {
    constexpr auto t = w22::makeTuple();
    static_assert(w22::size(t) == 0);
    EXPECT_EQ(w22::size(t), 0U);
}

}  // namespace
