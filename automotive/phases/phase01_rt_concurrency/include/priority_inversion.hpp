#ifndef AV_PHASE01_PRIORITY_INVERSION_HPP
#define AV_PHASE01_PRIORITY_INVERSION_HPP

namespace av {
namespace rt {

/// Outcome of one priority-inversion experiment.
struct InversionResult {
  /// False when the process lacks CAP_SYS_NICE / RLIMIT_RTPRIO, so no SCHED_FIFO
  /// thread could be created. Everything else is then meaningless.
  bool ran = false;

  /// How long the HIGH-priority thread sat blocked on the mutex. This is the
  /// number the whole experiment exists to produce.
  double high_blocked_ms = 0.0;

  /// Configured durations, echoed back so the test can reason about the result
  /// without hard-coding the constants twice.
  double critical_section_ms = 0.0;
  double medium_work_ms = 0.0;
};

/// Reproduces the Mars Pathfinder failure, on one CPU, with SCHED_FIFO.
///
///   LOW  (prio 10) takes the mutex, then works for critical_section_ms
///   HIGH (prio 30) tries to take the mutex and times how long it waits
///   MED  (prio 20) spins for medium_work_ms and NEVER TOUCHES THE MUTEX
///
/// Without priority inheritance, MED preempts LOW, LOW cannot release, and HIGH
/// waits for roughly (critical_section + medium_work) — a bound that depends on a
/// thread it shares nothing with. That is UNBOUNDED priority inversion.
///
/// With PTHREAD_PRIO_INHERIT, LOW temporarily runs at HIGH's priority while
/// holding the mutex, so MED cannot preempt it, and HIGH waits for roughly the
/// critical section alone.
///
/// All three threads are pinned to a single CPU: with more than one core they
/// would simply run in parallel and there would be no inversion to observe.
InversionResult run_priority_inversion(bool use_priority_inheritance);

}  // namespace rt
}  // namespace av

#endif  // AV_PHASE01_PRIORITY_INVERSION_HPP
