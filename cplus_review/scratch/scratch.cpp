// Week 0 proof-of-substrate. This file is DELIBERATELY broken.
//
//   ./build/asan-23/scratch/scratch_ub  use-after-free    -> ASAN must abort
//   ./build/asan-23/scratch/scratch_ub  leak              -> LeakSanitizer must report
//   ./build/ubsan-23/scratch/scratch_ub signed-overflow   -> UBSAN must report
//   ./build/ubsan-23/scratch/scratch_ub oob-shift         -> UBSAN must report

#include <cstdio>
#include <string_view>

namespace {

// Heap-use-after-free. ASAN reports the read, the free site, AND the alloc site.
int useAfterFree() {
    int* p = new int(42);
    delete p;
    return *p;                  // <-- ASAN fires here
}

// Never freed. LeakSanitizer (bundled into ASAN) reports it at exit.
int leak() {
    int* p = new int[1024];
    p[0] = 1;
    return p[0];                // <-- leaked on return
}

// Signed overflow is UB, NOT wraparound. This is the one that bites people:
// without UBSAN it "works", and the optimizer is entitled to assume it never
// happens -- which is how it silently deletes your bounds checks.
int signedOverflow() {
    int x = 2147483647;
    volatile int one = 1;       // volatile so it survives to runtime
    return x + one;             // <-- UBSAN fires here
}

// Shifting by >= the bit width is UB.
int oobShift() {
    volatile int shift = 33;
    int x = 1;
    return x << shift;          // <-- UBSAN fires here
}

void usage() {
    std::puts("usage: scratch_ub <use-after-free|leak|signed-overflow|oob-shift>");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return 2;
    }
    const std::string_view mode{argv[1]};

    if (mode == "use-after-free")       { std::printf("%d\n", useAfterFree()); }
    else if (mode == "leak")            { std::printf("%d\n", leak()); }
    else if (mode == "signed-overflow") { std::printf("%d\n", signedOverflow()); }
    else if (mode == "oob-shift")       { std::printf("%d\n", oobShift()); }
    else { usage(); return 2; }

    return 0;
}
