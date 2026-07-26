// W25 — CRTP & static polymorphism, side by side with the virtual version it
// replaces. Two hierarchies computing the same areas; the only difference is
// WHEN the call is resolved — runtime (vtable) vs compile time (CRTP).
//
// The point of CRTP is not merely speed. It is a STATIC INTERFACE: the base
// writes reusable logic ONCE, in terms of a primitive each derived type
// supplies, with the call bound at compile time so it inlines. Speed is the
// bonus; `std::enable_shared_from_this` and `std::ranges::view_interface` use
// CRTP for the interface, not the nanoseconds.
#pragma once

#include <numbers>

namespace w25 {

// =============================================== virtual — runtime dispatch ===
// One base pointer can hold any Shape; the concrete type is discovered at
// runtime through the vtable. That indirection is also what blocks inlining.
struct Shape {
    virtual double area() const = 0;
    virtual ~Shape() = default;
};

struct Circle : Shape {
    double r;
    explicit Circle(double radius) : r(radius) { }
    double area() const override { return std::numbers::pi * r * r; }
};

struct Square : Shape {
    double s;
    explicit Square(double side) : s(side) { }
    double area() const override { return s * s; }
};

// ================================================ CRTP — compile-time dispatch =
// The base is templated on the DERIVED type, so `self()` can static_cast down
// and call the derived primitive with the call fully visible to the optimizer.
// `area_impl` lives in each derived class; the base never declares it — the
// contract is enforced only by the fact that `self().area_impl()` must compile.
template <typename Derived>
class ShapeS {
public:
    double area() const { return self().area_impl(); }

    // Reuse built on the primitive — written ONCE here, inherited by every
    // shape, still fully inlined. This is the actual reason CRTP exists.
    double scaled_area(double k) const { return k * k * area(); }

protected:
    // The downcast is safe *because* Derived truly derives from ShapeS<Derived>.
    // Pass the wrong class (struct Bug : ShapeS<Other>) and this is silent UB —
    // no default diagnostic. A C++20 `requires derived_from<...>` can't sit on
    // the class template itself (Derived is incomplete here), so the guard, if
    // wanted, goes on the member functions. See the note.
    const Derived& self() const { return static_cast<const Derived&>(*this); }
};

class CircleS : public ShapeS<CircleS> {
public:
    explicit CircleS(double radius) : r_(radius) { }

private:
    friend class ShapeS<CircleS>;                 // let the base reach area_impl
    double area_impl() const { return std::numbers::pi * r_ * r_; }
    double r_;
};

class SquareS : public ShapeS<SquareS> {
public:
    explicit SquareS(double side) : s_(side) { }

private:
    friend class ShapeS<SquareS>;
    double area_impl() const { return s_ * s_; }
    double s_;
};

// A generic algorithm over the static interface: it accepts ANY CRTP shape by
// its base, yet every call inside resolves at compile time — no vtable, no base
// pointer, and it inlines straight through. The virtual world would take a
// `const Shape&` and pay dispatch on every call.
template <typename Derived>
double bigger_area(const ShapeS<Derived>& shape) {
    return shape.scaled_area(2.0);
}

}  // namespace w25
