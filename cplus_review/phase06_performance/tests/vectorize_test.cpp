// W44 -- SIMD / autovectorization, the CORRECTNESS half.
//
// A unit test cannot observe "did this loop vectorize?" -- that lives in the
// generated code, and is proved in notes/simd_vectorization.md from the
// compiler's own -fopt-info-vec output and objdump. What the test DOES pin down:
//
//   1. add_aliased and add_restrict compute the same thing on disjoint buffers.
//      The difference between them is entirely in the asm (restrict removes the
//      runtime aliasing guard), not in the result.
//   2. add_aliased stays correct even when the output overlaps the input --
//      which is exactly the case the compiler's runtime guard exists to protect.
//   3. dot() sums strictly left-to-right, matching a reference summed the same
//      way. A SIMD re-association would diverge; that divergence is why the
//      compiler refuses to vectorize it without -ffast-math.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "vectorize_kernels.hpp"

namespace {

std::vector<float> iota_floats(std::size_t n, float start) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = start + static_cast<float>(i);
    }
    return v;
}

}  // namespace

TEST(W44Vectorize, AliasedAndRestrictAgreeOnDisjointBuffers) {
    constexpr std::size_t kN = 1024;
    const auto a = iota_floats(kN, 1.0F);
    const auto b = iota_floats(kN, 1000.0F);

    std::vector<float> out_aliased(kN);
    std::vector<float> out_restrict(kN);
    cr::add_aliased(out_aliased.data(), a.data(), b.data(), kN);
    cr::add_restrict(out_restrict.data(), a.data(), b.data(), kN);

    for (std::size_t i = 0; i < kN; ++i) {
        const float expected = a[i] + b[i];
        EXPECT_FLOAT_EQ(out_aliased[i], expected);
        EXPECT_FLOAT_EQ(out_restrict[i], expected);
    }
}

// out == a: element-wise at the same index is actually safe, but the compiler
// must assume the worst overlap and guard for it. The result must be correct
// either way, which is what we assert here.
TEST(W44Vectorize, AliasedIsCorrectWhenOutputOverlapsInput) {
    constexpr std::size_t kN = 256;
    auto buf = iota_floats(kN, 5.0F);
    const auto b = iota_floats(kN, 0.5F);
    const auto original = buf;

    cr::add_aliased(buf.data(), buf.data(), b.data(), kN);  // out aliases a

    for (std::size_t i = 0; i < kN; ++i) {
        EXPECT_FLOAT_EQ(buf[i], original[i] + b[i]);
    }
}

TEST(W44Vectorize, DotMatchesLeftToRightReference) {
    constexpr std::size_t kN = 4096;
    const auto a = iota_floats(kN, 1.0F);
    const auto b = iota_floats(kN, 2.0F);

    float reference = 0.0F;
    for (std::size_t i = 0; i < kN; ++i) {
        reference += a[i] * b[i];
    }

    EXPECT_FLOAT_EQ(cr::dot(a.data(), b.data(), kN), reference);
}
