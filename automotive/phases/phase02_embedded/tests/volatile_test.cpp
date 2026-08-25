// W3 — What `volatile` does, and the far more important what it does NOT do.
//
// Two kinds of evidence here:
//   1. The compiler's own output (src/elision_probe.cpp -> .s at -O2). Deterministic.
//   2. A deliberate data race, quarantined behind DISABLED_ so the TSan gate stays
//      green. Run it by hand with --gtest_also_run_disabled_tests.

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// --- a very small assembly reader ----------------------------------------------

std::vector<std::string> read_lines(const char* path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) { lines.push_back(line); }
  return lines;
}

// Instruction lines are indented. Everything starting at column 0 is a label, and
// anything whose first non-blank character is '.' or '#' is an assembler
// directive or a comment. Only real instructions count as "the CPU does this".
bool is_instruction(const std::string& line) {
  if (line.empty() || (line[0] != '\t' && line[0] != ' ')) { return false; }
  const std::size_t i = line.find_first_not_of(" \t");
  return i != std::string::npos && line[i] != '.' && line[i] != '#';
}

std::size_t count_instructions_touching(const std::vector<std::string>& lines,
                                        const std::string& symbol) {
  std::size_t n = 0U;
  for (const std::string& line : lines) {
    if (is_instruction(line) && line.find(symbol) != std::string::npos) { ++n; }
  }
  return n;
}

// Everything between "name:" and the ".size name" directive that closes it.
std::vector<std::string> function_body(const std::vector<std::string>& lines,
                                       const std::string& name) {
  std::vector<std::string> body;
  const std::string label = name + ":";
  const std::string end = ".size";
  bool inside = false;
  for (const std::string& line : lines) {
    if (!inside) {
      if (line.rfind(label, 0) == 0) { inside = true; }
      continue;
    }
    if (line.find(end) != std::string::npos && line.find(name) != std::string::npos) { break; }
    body.push_back(line);
  }
  return body;
}

// A loop, at the level the CPU sees it: a local label followed later by a jump
// back to it. Counting "how many load instructions" would be the wrong unit —
// the compiler keeps ONE load inside a loop that runs ten times.
bool has_backward_branch(const std::vector<std::string>& body) {
  std::vector<std::string> labels;
  for (const std::string& line : body) {
    if (!line.empty() && line[0] == '.' && line.back() == ':') {
      labels.push_back(line.substr(0U, line.size() - 1U));
    } else if (is_instruction(line)) {
      for (const std::string& label : labels) {
        if (line.find(label) != std::string::npos) { return true; }
      }
    }
  }
  return false;
}

const std::vector<std::string>& probe_asm() {
  static const std::vector<std::string> lines = read_lines(AV_PROBE_ASM);
  return lines;
}

}  // namespace

// --- claim 1 & 2: volatile suppresses elision -----------------------------------

TEST(Volatile, ProbeAssemblyExists) {
  // If the probe failed to build, every test below would silently "pass" on an
  // empty file. Fail loudly here instead.
  ASSERT_FALSE(probe_asm().empty()) << "no assembly at " << AV_PROBE_ASM;
}

TEST(Volatile, PlainReadsAreCollapsed) {
  const std::vector<std::string> body = function_body(probe_asm(), "read_plain_ten_times");
  ASSERT_FALSE(body.empty());

  // Ten reads in the source. The compiler proved nothing can change the value in
  // between, so it reads ONCE and multiplies:
  //     movl g_plain(%rip), %eax
  //     leal (%rax,%rax,4), %eax   ; x5
  //     addl %eax, %eax            ; x2  -> x10
  EXPECT_EQ(count_instructions_touching(body, "g_plain"), 1U);
  EXPECT_FALSE(has_backward_branch(body)) << "the loop should be gone entirely";
}

TEST(Volatile, VolatileReadsAllSurvive) {
  const std::vector<std::string> body = function_body(probe_asm(), "read_volatile_ten_times");
  ASSERT_FALSE(body.empty());

  // Same loop, one keyword different, and the loop SURVIVES — because reading a
  // volatile object is an OBSERVABLE SIDE EFFECT the compiler may not invent,
  // remove, or reorder against other volatile accesses. All ten reads happen:
  //     .L4: movl g_volatile(%rip), %ecx
  //          addl %ecx, %edx
  //          subl $1, %eax
  //          jne  .L4
  EXPECT_TRUE(has_backward_branch(body)) << "the ten volatile reads were optimised away";
  EXPECT_GE(count_instructions_touching(body, "g_volatile"), 1U);
}

// --- claim 3: volatile is NOT atomic --------------------------------------------

TEST(Volatile, IncrementIsNotOneInstruction) {
#if !defined(__x86_64__)
  GTEST_SKIP() << "the lock-prefix check below is x86-64 specific";
#else
  const std::vector<std::string> vol = function_body(probe_asm(), "inc_volatile");
  const std::vector<std::string> ato = function_body(probe_asm(), "inc_atomic");
  ASSERT_FALSE(vol.empty());
  ASSERT_FALSE(ato.empty());

  // ++volatile touches memory TWICE — a read, then a separate write:
  //     movl g_volatile(%rip), %eax
  //     addl $1, %eax
  //     movl %eax, g_volatile(%rip)
  // Anything at all can happen in the gap between those two instructions.
  EXPECT_EQ(count_instructions_touching(vol, "g_volatile"), 2U)
      << "expected a separate load and store";
  EXPECT_EQ(count_instructions_touching(vol, "lock"), 0U)
      << "volatile grants no atomicity — there must be no lock prefix";

  // fetch_add touches memory ONCE, indivisibly:
  //     lock addl $1, g_atomic(%rip)
  // There is no gap. THAT is what atomic means, and volatile never provides it.
  EXPECT_EQ(count_instructions_touching(ato, "g_atomic"), 1U)
      << "expected a single read-modify-write instruction";
  EXPECT_EQ(count_instructions_touching(ato, "lock"), 1U)
      << "expected a lock-prefixed instruction from std::atomic::fetch_add";
#endif
}

// The visceral version of the same fact. It IS a data race by definition, so it
// lives behind DISABLED_ and never runs in the gate.
//   ./phase02_volatile_test --gtest_also_run_disabled_tests
TEST(Volatile, DISABLED_IncrementLosesUpdates) {
  constexpr int kThreads = 4;
  constexpr int kPerThread = 200000;

  volatile int counter = 0;

  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&counter]() {
      // Written out longhand, because C++20 DEPRECATED ++ and += on volatile
      // (P1152R4) for exactly the reason this test demonstrates: they look like
      // one operation and are not. Spelling it as read-then-write is honest.
      for (int i = 0; i < kPerThread; ++i) { counter = counter + 1; }
    });
  }
  for (std::thread& w : workers) { w.join(); }

  const int expected = kThreads * kPerThread;
  const int actual = counter;
  std::cout << "expected " << expected << ", got " << actual << " — lost "
            << (expected - actual) << " increments\n";

  // Deliberately asserting the BROKEN outcome: volatile did not protect anything.
  EXPECT_LT(actual, expected) << "no updates were lost this run; try more threads";
}
