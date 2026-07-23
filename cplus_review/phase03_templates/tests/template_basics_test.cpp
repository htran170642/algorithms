// W21 — deduction, CTAD, specialization vs overloading, lazy instantiation.
// Every assertion here was a quiz answer first. Predict, then verify.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

namespace {

// ---------- Q1: three deduction forms, checked at compile time ----------
template <typename T> constexpr const char* byValue(T)   { return "by-value"; }
template <typename T> constexpr const char* byRef(T&)    { return "by-ref"; }

TEST(Deduction, ByValueStripsConstAndRef) {
    int x = 42;
    const int cx = x;
    const int& rx = cx;

    // by-value: const and ref are both stripped — you own the copy.
    // The extra parens are load-bearing: EXPECT_TRUE is a macro, and the
    // preprocessor splits on the comma inside is_same_v< , > long before it
    // knows what a template is. Without them: "passed 2 arguments, takes 1".
    static_assert(std::is_same_v<decltype(byValue(cx)), const char*>);
    EXPECT_TRUE((std::is_same_v<int, std::decay_t<decltype(cx)>>));
    EXPECT_TRUE((std::is_same_v<int, std::decay_t<decltype(rx)>>));
}

// ---------- Q2: CTAD deduces const char*, not std::string ----------
template <typename T>
struct Box {
    explicit Box(T v) : value(std::move(v)) { }
    T value;
};

}  // namespace

// A deduction guide is what makes "hello" become a std::string. It lives in a
// NAMED namespace on purpose: a guide has no body — it isn't a function, just an
// instruction to the deduction step — so inside an anonymous namespace GCC gives
// it internal linkage and then reports "declared 'static' but never defined".
namespace w21 {
template <typename T>
struct SafeBox {
    explicit SafeBox(T v) : value(std::move(v)) { }
    T value;
};
SafeBox(const char*) -> SafeBox<std::string>;
}  // namespace w21

namespace {

TEST(Ctad, RawArrayDecaysToPointer) {
    Box b{"hello"};
    EXPECT_TRUE((std::is_same_v<decltype(b), Box<const char*>>));

    w21::SafeBox s{"hello"};
    EXPECT_TRUE((std::is_same_v<decltype(s), w21::SafeBox<std::string>>));
    EXPECT_EQ(s.value, "hello");
}

// ---------- Q3: a plain function beats a template; the specialization hides ----------
template <typename T> const char* pick(T)   { return "generic template"; }
// [[maybe_unused]] is the whole lesson in one attribute. GCC rejects this line
// with -Wunused-function: "defined but not used" — the compiler independently
// proving that a full specialization loses to a plain function and becomes dead
// code. Kept, silenced, as evidence.
template <> [[maybe_unused]] const char* pick(int) { return "specialization"; }
                             const char* pick(int) { return "plain function"; }

TEST(Overloading, PlainFunctionWinsOverSpecialization) {
    // Overload resolution runs BEFORE specializations are even considered.
    EXPECT_STREQ(pick(42), "plain function");
    EXPECT_STREQ(pick(4.2), "generic template");
}

// ---------- Q4: member functions are only instantiated when called ----------
template <typename T>
struct Widget {
    int ok() const { return 1; }
    void bad() const { T::this_does_not_exist(); }   // never called => never checked
};

TEST(LazyInstantiation, UnusedMemberIsNeverCompiled) {
    Widget<int> w;
    EXPECT_EQ(w.ok(), 1);            // calling w.bad() here would break the build
}

struct NoCompare { int x; };

TEST(LazyInstantiation, ContainerAcceptsPartiallyUsableType) {
    // vector<NoCompare> is perfectly legal — until you call something needing <
    std::vector<NoCompare> v;
    v.push_back(NoCompare{1});
    EXPECT_EQ(v.front().x, 1);       // std::sort(v...) would be the 200-line error
}

}  // namespace
