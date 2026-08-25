// W3 — Evidence from the compiler, not from a claim.
//
// This file is NEVER LINKED. The build compiles it to ASSEMBLY at -O2 and the
// test reads the .s file. That is the whole point: what `volatile` does and does
// not do is a statement about generated code, so the generated code is the proof.
//
// Everything is extern "C" so the symbols in the .s file are the names written
// here, not Itanium-mangled ones the test would have to decode.

#include <atomic>

extern "C" {

int g_plain = 0;
volatile int g_volatile = 0;

// --- claim 1: without volatile the compiler is allowed to skip the reads -------

// Nothing inside this loop can change g_plain, and the compiler knows it. It is
// free to read once and multiply — or to hoist the load out of the loop entirely.
int read_plain_ten_times(void) {
  int sum = 0;
  for (int i = 0; i < 10; ++i) { sum += g_plain; }
  return sum;
}

// --- claim 2: volatile forbids exactly that ------------------------------------

// Every read is an observable side effect. The compiler must emit all ten, in
// order. This is why a hardware register MUST be volatile: its value changes
// without anything in this program assigning to it.
int read_volatile_ten_times(void) {
  int sum = 0;
  for (int i = 0; i < 10; ++i) { sum += g_volatile; }
  return sum;
}

// --- claim 3: volatile is NOT atomic -------------------------------------------

// ++ on a volatile is still load -> add -> store. Three separate steps, and the
// CPU can be interrupted between any two of them. Another thread reading in that
// gap sees the old value; another thread writing in that gap loses its write.
//
// C++20 DEPRECATED this spelling (P1152R4) precisely because it reads like one
// operation. Written longhand so the -20 presets stay warning-free; the emitted
// assembly is identical, which is itself the point.
void inc_volatile(void) { g_volatile = g_volatile + 1; }

}  // extern "C"

// The contrast. fetch_add is ONE indivisible instruction (lock-prefixed on
// x86-64). Nothing can observe a half-finished state.
std::atomic<int> g_atomic{0};

extern "C" void inc_atomic(void) { g_atomic.fetch_add(1, std::memory_order_relaxed); }
