#include <gtest/gtest.h>

#include <cstdint>

namespace {

// ============================================== multiple inheritance: 2 vptrs
struct A { virtual ~A() = default; virtual int fa() const { return 1; } int a = 10; };
struct B { virtual ~B() = default; virtual int fb() const { return 2; } int b = 20; };
struct C : A, B {
    int fa() const override { return 100; }
    int fb() const override { return 200; }
    int c = 30;
};

// ============================================== the diamond
struct Animal { int age = 5; };
struct DogPlain : Animal {};
struct CatPlain : Animal {};
struct ChimeraPlain : DogPlain, CatPlain {};   // TWO Animals — age is ambiguous

struct DogV : virtual Animal {};
struct CatV : virtual Animal {};
struct ChimeraV : DogV, CatV {};               // ONE shared Animal

// ============================================== EBO
struct Empty {};
struct HasEmptyMember { Empty e; int x; };      // Empty takes a byte + padding
struct DerivesEmpty : Empty { int x; };         // Empty vanishes

std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

}  // namespace

// ================= 1. casting to the second base SHIFTS the pointer
TEST(W8Model, SecondBasePointerIsOffset) {
    C c;

    const A* pa = &c;
    const B* pb = &c;

    EXPECT_EQ(addr(pa), addr(&c));            // first base: same address
    EXPECT_NE(addr(pb), addr(&c));            // SECOND base: shifted!
    EXPECT_GT(addr(pb), addr(pa));            // B part lives after A part

    // ...yet dispatch still finds the right override through either pointer
    EXPECT_EQ(pa->fa(), 100);
    EXPECT_EQ(pb->fb(), 200);
}

// ================= 2. round-trip through the second base is still the same object
TEST(W8Model, CastBackRecoversTheObject) {
    C c;
    const B* pb = &c;                          // shifted pointer
    const C* back = static_cast<const C*>(pb); // static_cast subtracts the offset

    EXPECT_EQ(addr(back), addr(&c));           // exactly &c again
    // reinterpret_cast<const C*>(pb) would NOT do this — it wouldn't adjust.
}

// ================= 3. multiple inheritance: two vptrs, and TAIL PADDING REUSE
TEST(W8Model, TwoVptrsAndTailPaddingReuse) {
    // The naive sum is wrong: A(16) + B(16) + c(4) is NOT 40.
    //
    // sizeof(A)==16 only as a STANDALONE object (12 bytes data + 4 tail pad).
    // As a base SUBOBJECT, the tail padding is not part of its data size, and
    // the derived class packs its own members into it:
    //
    //   A::vptr @0   A::a @8            (A data: bytes 0..11)
    //   B::vptr @16  B::b @24           (B data: bytes 16..27)
    //   C::c    @28  <-- lands in B's tail padding, no new space needed
    //
    // => sizeof(C) == 32, not 40. A class's size is NOT the sum of its bases'.
    EXPECT_EQ(sizeof(A), 16u);
    EXPECT_EQ(sizeof(B), 16u);
    EXPECT_EQ(sizeof(C), 32u);
}

// ================= 4. virtual inheritance collapses the diamond
TEST(W8Model, VirtualInheritanceKeepsOneAnimal) {
    ChimeraV chimera;

    // With plain inheritance chimera.age would be ambiguous (two Animals).
    // With virtual inheritance there is exactly one, reachable unambiguously.
    chimera.age = 42;
    EXPECT_EQ(static_cast<DogV&>(chimera).age, 42);   // same Animal...
    EXPECT_EQ(static_cast<CatV&>(chimera).age, 42);   // ...seen through both

    // The price: virtual inheritance makes the object bigger (vbase pointers).
    EXPECT_GT(sizeof(ChimeraV), sizeof(ChimeraPlain));
}

// ================= 5. Empty Base Optimization
TEST(W8Model, EmptyBaseCostsZeroButEmptyMemberCostsAByte) {
    EXPECT_EQ(sizeof(Empty), 1u);              // a standalone empty object needs an address
    EXPECT_EQ(sizeof(HasEmptyMember), 8u);     // as a MEMBER: 1 byte + padding for int
    EXPECT_EQ(sizeof(DerivesEmpty), 4u);       // as a BASE: it vanishes (EBO)
}
