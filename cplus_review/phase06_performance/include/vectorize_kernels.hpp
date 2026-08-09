#pragma once

#include <cstddef>

// W44 -- SIMD awareness / autovectorization. Three tiny kernels whose ONLY job
// is to make the compiler's decisions visible. Compile them alone and read
// `-fopt-info-vec` + the asm; the unit test next door only proves they are
// numerically correct.
namespace cr {

// out[i] = a[i] + b[i], element-wise.
//
// No `restrict`: the compiler cannot prove `out` does not overlap `a`/`b`, so it
// emits a runtime overlap check that picks a vectorized path when the buffers
// are disjoint and a scalar path when they are not ("loop versioning for
// aliasing"). Correct, but with extra branches and code.
void add_aliased(float* out, const float* a, const float* b, std::size_t n);

// Identical math. `__restrict` is a promise that the buffers never overlap, so
// the compiler drops the guard and keeps only the vectorized loop. This is the
// single most effective knob for unlocking autovectorization of pointer code.
void add_restrict(float* __restrict out, const float* __restrict a,
                  const float* __restrict b, std::size_t n);

// sum of a[i] * b[i].
//
// Floating-point `+` is NOT associative, so summing into SIMD lanes and folding
// them re-orders the additions and changes the result bit-for-bit. The compiler
// therefore REFUSES to vectorize this reduction unless you opt in with
// -ffast-math (or `#pragma omp simd reduction`). This is why "8 lanes != 8x".
float dot(const float* a, const float* b, std::size_t n);

}  // namespace cr
