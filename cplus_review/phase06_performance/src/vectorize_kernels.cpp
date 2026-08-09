#include "vectorize_kernels.hpp"

// Kept in a separate translation unit on purpose: the functions have external
// linkage and are NOT inlined into the test, so their symbols survive and we can
// `objdump -d` them to see addps (SSE, 4 lanes) vs vaddps (AVX2, 8 lanes), and
// count the aliasing-guard branches that `__restrict` removes.
namespace cr {

void add_aliased(float* out, const float* a, const float* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a[i] + b[i];
    }
}

void add_restrict(float* __restrict out, const float* __restrict a,
                  const float* __restrict b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a[i] + b[i];
    }
}

float dot(const float* a, const float* b, std::size_t n) {
    float sum = 0.0F;
    for (std::size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];  // strict left-to-right; not vectorized by default
    }
    return sum;
}

}  // namespace cr
