#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

// ================= 1. a minimal custom allocator: counts allocations
namespace {

template <typename T>
struct CountingAllocator {
    using value_type = T;
    static inline std::size_t allocations = 0;

    CountingAllocator() = default;
    template <typename U> CountingAllocator(const CountingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        ++allocations;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }

    template <typename U> bool operator==(const CountingAllocator<U>&) const noexcept { return true; }
};

}  // namespace

TEST(W20Alloc, CustomAllocatorIsUsedByVector) {
    CountingAllocator<int>::allocations = 0;
    std::vector<int, CountingAllocator<int>> v;

    for (int i = 0; i < 1000; ++i) v.push_back(i);

    EXPECT_GT(CountingAllocator<int>::allocations, 0u);   // our allocate() ran
    EXPECT_LT(CountingAllocator<int>::allocations, 15u);  // ~log2 reallocs (W5 geometric)
    EXPECT_EQ(v[500], 500);
}

// ================= 2. pmr: all pmr::vector<int> are the SAME TYPE
TEST(W20Alloc, PmrVectorsShareOneType) {
    std::array<std::byte, 4096> buf1{};
    std::array<std::byte, 4096> buf2{};
    std::pmr::monotonic_buffer_resource pool1{buf1.data(), buf1.size()};
    std::pmr::monotonic_buffer_resource pool2{buf2.data(), buf2.size()};

    std::pmr::vector<int> a{&pool1};
    std::pmr::vector<int> b{&pool2};      // different pool, SAME type

    a.push_back(1);
    b = a;                                // compiles: same type. (b keeps pool2.)
    EXPECT_EQ(b[0], 1);

    // proof they're the same type:
    static_assert(std::is_same_v<decltype(a), decltype(b)>);
}

// ================= 3. monotonic_buffer_resource allocates from a stack buffer
TEST(W20Alloc, MonotonicResourceUsesTheProvidedBuffer) {
    std::array<std::byte, 1024> buf{};
    std::pmr::monotonic_buffer_resource pool{buf.data(), buf.size()};

    std::pmr::vector<int> v{&pool};
    for (int i = 0; i < 100; ++i) v.push_back(i);

    // The vector's data must live INSIDE our stack buffer, not on the heap.
    auto* data = reinterpret_cast<const std::byte*>(v.data());
    EXPECT_GE(data, buf.data());
    EXPECT_LT(data, buf.data() + buf.size());   // points into buf -> no heap used
    EXPECT_EQ(v[99], 99);
}
