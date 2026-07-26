// W25 — CRTP / static polymorphism. The CRTP shapes must compute exactly what
// the virtual shapes do; the difference is only WHEN the call binds. A couple of
// static_asserts prove the object layout: the CRTP object carries no vptr.

#include <gtest/gtest.h>

#include <type_traits>

#include "w25/shapes.hpp"

namespace {

TEST(CRTP, ComputesTheSameAreaAsVirtual) {
    EXPECT_DOUBLE_EQ(w25::Circle(2.0).area(), w25::CircleS(2.0).area());
    EXPECT_DOUBLE_EQ(w25::Square(3.0).area(), w25::SquareS(3.0).area());
}

TEST(CRTP, BaseReusesTheDerivedPrimitive) {
    w25::CircleS c(2.0);
    // scaled_area lives ONCE in the base, built on area_impl of the derived.
    EXPECT_DOUBLE_EQ(c.scaled_area(3.0), 9.0 * c.area());
}

TEST(CRTP, GenericAlgorithmTakesAnyStaticShape) {
    w25::CircleS c(1.0);
    w25::SquareS s(2.0);
    EXPECT_DOUBLE_EQ(w25::bigger_area(c), c.scaled_area(2.0));
    EXPECT_DOUBLE_EQ(w25::bigger_area(s), s.scaled_area(2.0));
}

// Static polymorphism means no per-object machinery: the CRTP base is empty, so
// EBO collapses it and the object is just its own data. The virtual object pays
// for a vptr. This is the layout half of the speed story.
static_assert(sizeof(w25::CircleS) == sizeof(double),  "CRTP object has no vptr");
static_assert(sizeof(w25::Circle)  >  sizeof(double),  "virtual object carries a vptr");

// The flip side (quiz Q4): each CRTP base is a DISTINCT type, so there is no
// common base to point at — you cannot build a heterogeneous container of them.
static_assert(!std::is_same_v<w25::ShapeS<w25::CircleS>, w25::ShapeS<w25::SquareS>>,
              "ShapeS<CircleS> and ShapeS<SquareS> are unrelated types");

}  // namespace
