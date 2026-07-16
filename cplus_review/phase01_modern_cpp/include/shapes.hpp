#pragma once

// Polymorphism, and the three ways it breaks:
//   1. a non-virtual destructor deleted through a base pointer = UB
//   2. passing by value SLICES the derived part off
//   3. a signature that doesn't match creates a NEW function, silently
//
// Also: NVI (Non-Virtual Interface) — the pattern most codebases should
// use and don't.

#include <string>
#include <utility>

namespace cr {

// ---------------------------------------------- correct: virtual destructor
class Shape {
public:
    Shape() = default;
    Shape(const Shape&) = default;
    Shape& operator=(const Shape&) = default;
    Shape(Shape&&) noexcept = default;
    Shape& operator=(Shape&&) noexcept = default;

    virtual ~Shape() { ++dtor_calls; }        // virtual: ~Circle WILL run

    [[nodiscard]] virtual std::string name() const { return "Shape"; }
    [[nodiscard]] virtual double area() const = 0;   // pure virtual

    static inline int dtor_calls = 0;
    static void reset() noexcept { dtor_calls = 0; }
};

class Circle final : public Shape {              // final: no further overriding
public:
    explicit Circle(double r) : r_(r) {}
    ~Circle() override { ++derived_dtor_calls; }

    [[nodiscard]] std::string name() const override { return "Circle"; }
    [[nodiscard]] double area() const override { return 3.14159265 * r_ * r_; }

    static inline int derived_dtor_calls = 0;
    static void resetDerived() noexcept { derived_dtor_calls = 0; }

private:
    double r_;
};

// ============================================================================
// DELIBERATELY BROKEN, below this line.
//
// Week 0's -Werror already REFUSES to compile these, which is the real lesson:
//
//   error: 'cr::BadBase' has virtual functions and accessible non-virtual
//          destructor                              [-Werror=non-virtual-dtor]
//   error: 'virtual std::string cr::SigBase::speak() const' was hidden
//                                                  [-Werror=overloaded-virtual=]
//
// GCC sees both traps unaided. On a project without these warnings they compile
// silently and the bug reaches production. We suppress locally — with a reason —
// purely so the tests can MEASURE the damage. Never loosen the global config.
// ============================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Woverloaded-virtual"

// ---------------------------------------- the trap: NON-virtual destructor
// Deleting one of these through a BadBase* is undefined behaviour: ~BadDerived
// never runs. The test below proves the destructor is skipped.
class BadBase {
public:
    ~BadBase() = default;                     // NOT virtual  <-- the bug
    [[nodiscard]] virtual std::string name() const { return "BadBase"; }
};

class BadDerived : public BadBase {
public:
    ~BadDerived() { ++dtor_calls; }           // will NOT run via BadBase*
    [[nodiscard]] std::string name() const override { return "BadDerived"; }

    static inline int dtor_calls = 0;
    static void reset() noexcept { dtor_calls = 0; }
};

// ------------------------------------------- the silent trap: no `override`
class SigBase {
public:
    virtual ~SigBase() = default;
    [[nodiscard]] virtual std::string speak() const { return "base"; }   // note: const
};

class SigBroken : public SigBase {
public:
    // NOT const -> a different signature -> this overrides NOTHING. It quietly
    // creates a brand-new function that is never reached through a SigBase*.
    // Adding `override` turns this into:
    //     error: marked 'override', but does not override
    [[nodiscard]] std::string speak() { return "broken"; }
};

#pragma GCC diagnostic pop
// ============================ end of deliberately broken code ===============

class SigFixed : public SigBase {
public:
    [[nodiscard]] std::string speak() const override { return "fixed"; }
};

// ------------------------------------------------------- NVI pattern
// Public interface is NON-virtual; the customisation point is private virtual.
// The base keeps control of the pre/post steps and subclasses cannot skip them.
class Report {
public:
    virtual ~Report() = default;

    std::string generate() const {            // NON-virtual: the contract
        return "<<" + body() + ">>";          // base owns the wrapping
    }

private:
    [[nodiscard]] virtual std::string body() const = 0;   // the hook
};

class SalesReport final : public Report {
private:
    [[nodiscard]] std::string body() const override { return "sales"; }
};

}  // namespace cr
