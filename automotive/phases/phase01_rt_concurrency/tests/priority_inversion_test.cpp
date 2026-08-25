// W2 — the Mars Pathfinder failure, reproduced and then fixed, with numbers.
//
// These tests need SCHED_FIFO, which most desktops deny by default. Rather than
// fail the gate on a permissions issue, they SKIP with the exact fix in the
// message. A skipped test that tells you how to un-skip it is honest; a test that
// silently passes without measuring anything is not.

#include "priority_inversion.hpp"

#include <gtest/gtest.h>

#include <iostream>

namespace {

constexpr const char* kNoRtPermission =
    "SCHED_FIFO denied. Grant realtime priority with either:\n"
    "  sudo setcap cap_sys_nice+ep <this binary>\n"
    "or add to /etc/security/limits.conf and re-login:\n"
    "  <user>  -  rtprio  99\n"
    "Until then the priority-inversion numbers are NOT measured.";

}  // namespace

TEST(PriorityInversion, WithoutInheritanceHighWaitsForAnUnrelatedThread) {
  const av::rt::InversionResult r = av::rt::run_priority_inversion(false);
  if (!r.ran) { GTEST_SKIP() << kNoRtPermission; }

  std::cout << "  [no PI] HIGH blocked for " << r.high_blocked_ms << " ms"
            << " (critical section = " << r.critical_section_ms
            << " ms, MED work = " << r.medium_work_ms << " ms)\n";

  // HIGH shares NOTHING with MED, yet it waits for MED to finish. The bound on
  // its blocking time depends on a thread it has no relationship with — that is
  // what "unbounded" means, and why the WCET cannot be computed.
  EXPECT_GT(r.high_blocked_ms, r.medium_work_ms * 0.5)
      << "expected HIGH to be starved by MED";
}

TEST(PriorityInversion, PriorityInheritanceBoundsTheWaitToTheCriticalSection) {
  const av::rt::InversionResult r = av::rt::run_priority_inversion(true);
  if (!r.ran) { GTEST_SKIP() << kNoRtPermission; }

  std::cout << "  [with PI] HIGH blocked for " << r.high_blocked_ms << " ms"
            << " (critical section = " << r.critical_section_ms << " ms)\n";

  // With PTHREAD_PRIO_INHERIT, LOW runs at HIGH's priority while it holds the
  // mutex, so MED cannot preempt it. HIGH now waits for the critical section and
  // nothing else — a bound that IS computable.
  EXPECT_LT(r.high_blocked_ms, r.medium_work_ms * 0.5)
      << "priority inheritance should keep MED from starving LOW";
}

TEST(PriorityInversion, InheritanceMeasurablyReducesBlocking) {
  const av::rt::InversionResult without = av::rt::run_priority_inversion(false);
  const av::rt::InversionResult with = av::rt::run_priority_inversion(true);
  if (!without.ran || !with.ran) { GTEST_SKIP() << kNoRtPermission; }

  const double speedup = without.high_blocked_ms / with.high_blocked_ms;
  std::cout << "  no PI = " << without.high_blocked_ms << " ms, "
            << "with PI = " << with.high_blocked_ms << " ms, "
            << "improvement = " << speedup << "x\n";

  EXPECT_LT(with.high_blocked_ms, without.high_blocked_ms)
      << "the whole point of the protocol";
}
