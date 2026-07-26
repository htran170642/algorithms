// W27 — struct layout, alignment, padding, alignas.
//
// This header proves the three layout laws by CONSTRUCTION — every claim below
// is checked with static_assert in layout_test.cpp, so nothing here is "trust
// me". These structs are throwaway specimens for the microscope, not a library.
//
//   Law 1 — each member sits at an offset that is a multiple of its alignof
//            (internal padding).
//   Law 2 — sizeof(struct) is rounded UP to a multiple of alignof(struct)
//            (= the largest member alignment) so that arrays keep every element
//            aligned (trailing padding).
//   Law 3 — reordering members largest-first removes INTERNAL padding; only a
//            size that already fits removes TRAILING padding.

#ifndef W27_LAYOUT_HPP
#define W27_LAYOUT_HPP

#include <cstddef>
#include <cstdint>

namespace w27 {

// --- The classic misordered struct -----------------------------------------
// char at 0, then 7 bytes of padding to align the double to offset 8, the
// double at 8..15, char at 16, then 7 bytes of TRAILING padding so sizeof is a
// multiple of alignof(double)==8.  10 bytes of data -> 24 bytes on the wire.
struct Bad {
    char   a;   // offset 0
    double b;   // offset 8   (7 bytes internal padding before it)
    char   c;   // offset 16  (7 bytes trailing padding after it)
};

// --- Same fields, reordered largest-first -----------------------------------
// double at 0..7, char at 8, char at 9. 10 bytes of data, rounded up to 16 by
// Law 2. Reordering killed the 7-byte INTERNAL gap; the 6-byte TRAILING pad
// survives because 10 is not a multiple of 8.
struct Good {
    double b;   // offset 0
    char   a;   // offset 8
    char   c;   // offset 9   (6 bytes trailing padding after it)
};

// --- A layout with NO padding at all ----------------------------------------
// Every member's size already lands the next one on its required boundary, and
// the total (16) is a multiple of alignof(std::uint64_t)==8. sizeof == sum.
struct Snug {
    std::uint64_t x;  // 8, offset 0
    std::uint32_t y;  // 4, offset 8
    std::uint16_t z;  // 2, offset 12
    std::uint8_t  w;  // 1, offset 14
    std::uint8_t  v;  // 1, offset 15   -> 16 total, no padding
};

// --- alignas WIDENS: it can only raise alignment, never shrink sizeof --------
// alignas(64) forces the object onto a 64-byte boundary, so the struct's
// alignment becomes 64 and its size is padded UP to 64 even though it holds one
// int. This is the shape you reach for to give a hot counter its own cache line
// and dodge false sharing (measured for real in W31).
//
// NOTE: we hardcode 64 rather than std::hardware_destructive_interference_size
// because GCC emits -Winterference-size (ABI-stability warning) on that symbol,
// and this repo builds warnings-as-errors. 64 is the x86-64 / ARM64 line size.
struct alignas(64) CacheLinePadded {
    std::int64_t counter;   // 8 bytes of payload, 56 bytes of padding to fill 64
};

// --- [[no_unique_address]] SHRINKS: the mirror image of alignas --------------
// An empty type normally still costs 1 byte (every object needs a distinct
// address). [[no_unique_address]] (C++20) lets an empty member overlap the next
// field, so a stateless policy/comparator/allocator adds zero size.
struct Empty { };  // stateless tag

struct WithoutAttr {
    Empty        e;   // costs 1 byte + 3 padding to align the int
    std::int32_t n;   // sizeof == 8
};

struct WithAttr {
    [[no_unique_address]] Empty e;   // overlaps n, contributes nothing
    std::int32_t                n;   // sizeof == 4
};

}  // namespace w27

#endif  // W27_LAYOUT_HPP
