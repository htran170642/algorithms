#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "shapes.hpp"

using namespace cr;

// ============================================== 1. dynamic dispatch works
TEST(W7Virtual, DispatchGoesThroughTheVtable) {
    std::unique_ptr<Shape> s = std::make_unique<Circle>(2.0);

    EXPECT_EQ(s->name(), "Circle");            // NOT "Shape" — resolved at runtime
    EXPECT_NEAR(s->area(), 12.566, 0.01);
}

// ============================== 2. virtual dtor: the derived dtor actually runs
TEST(W7Virtual, VirtualDtorRunsTheDerivedDestructor) {
    Shape::reset();
    Circle::resetDerived();

    {
        std::unique_ptr<Shape> s = std::make_unique<Circle>(1.0);
    }   // deleted through Shape* — but ~Shape is virtual

    EXPECT_EQ(Circle::derived_dtor_calls, 1);  // ~Circle RAN
    EXPECT_EQ(Shape::dtor_calls, 1);           // then ~Shape
}

// ================= 3. THE BUG: non-virtual dtor => derived dtor is SKIPPED
TEST(W7Virtual, NonVirtualDtorSkipsTheDerivedDestructor) {
    BadDerived::reset();

    {
        BadBase* p = new BadDerived;
        // -Werror=delete-non-virtual-dtor already refuses this line:
        //   error: deleting object of polymorphic class type 'cr::BadBase'
        //          which has non-virtual destructor might cause undefined behavior
        // Suppressed HERE, deliberately, so the test can measure the damage.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"
        delete p;        // UB. ~BadDerived never runs.
#pragma GCC diagnostic pop
    }

    EXPECT_EQ(BadDerived::dtor_calls, 0);      // <-- ZERO. It was never called.
    // If BadDerived owned a vector/string/file handle, it would leak here —
    // and the standard says this is undefined, not merely leaky.
}

// ============================================== 4. object slicing
TEST(W7Virtual, PassingByValueSlicesTheDerivedPartOff) {
    SigFixed derived;
    SigBase& ref = derived;

    EXPECT_EQ(ref.speak(), "fixed");           // through a reference: polymorphic

    SigBase sliced = derived;                  // <-- COPY into a SigBase
    EXPECT_EQ(sliced.speak(), "base");         // the Fixed part is GONE
    // sliced's vptr now points at SigBase's vtable. Polymorphism died quietly.
}

// ======================== 5. wrong signature => a new function, not an override
TEST(W7Virtual, MismatchedSignatureSilentlyFailsToOverride) {
    SigBroken broken;

    EXPECT_EQ(broken.speak(), "broken");       // called directly: fine

    SigBase& as_base = broken;
    EXPECT_EQ(as_base.speak(), "base");        // <-- through the base: "base"!
    // SigBroken::speak() (non-const) never overrode SigBase::speak() const.
    // It created a NEW function. `override` would have caught this at compile time.
}

// ============================================== 6. NVI: base keeps control
TEST(W7Virtual, NviBaseOwnsTheWrapping) {
    SalesReport r;
    const Report& base = r;

    EXPECT_EQ(base.generate(), "<<sales>>");   // subclass supplied "sales"...
                                               // ...base supplied the << >>
    // The subclass cannot skip the wrapping: generate() is not virtual.
}

// ============================================== 7. sizeof: the price of virtual
namespace {
struct Plain   { int x; void f() {} };
struct Virtual { int x; virtual void f() {} virtual ~Virtual() = default; };
}  // namespace

TEST(W7Virtual, VirtualCostsEightBytesPlusPadding) {
    EXPECT_EQ(sizeof(Plain), 4u);
    EXPECT_EQ(sizeof(Virtual), 16u);           // 8 (vptr) + 4 (int) + 4 (padding)

    Virtual v;
    const auto offset = reinterpret_cast<char*>(&v.x) - reinterpret_cast<char*>(&v);
    EXPECT_EQ(offset, 8);                      // vptr sits FIRST, x comes after
}
