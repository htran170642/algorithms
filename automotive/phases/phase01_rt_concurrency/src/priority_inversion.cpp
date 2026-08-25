// W2 — Unbounded priority inversion, measured, then fixed.
//
// Raw pthreads rather than std::thread: the scheduling policy must be set in the
// thread ATTRIBUTES so the thread starts at the right priority. Creating a
// std::thread and fixing its priority afterwards leaves a window where it already
// runs at the default policy — a race that quietly ruins the experiment.

#include "priority_inversion.hpp"

#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <atomic>

namespace av {
namespace rt {
namespace {

constexpr int kPrioLow = 10;
constexpr int kPrioMed = 20;
constexpr int kPrioHigh = 30;

constexpr double kCriticalSectionMs = 50.0;
constexpr double kMediumWorkMs = 200.0;

/// CLOCK_MONOTONIC, never CLOCK_REALTIME: realtime jumps when NTP steps the clock
/// or the timezone changes, which would silently corrupt a duration. Measuring
/// elapsed time with a wall clock is a bug — revisited properly in W6.
double now_ms() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) * 1000.0 +
         static_cast<double>(ts.tv_nsec) / 1000000.0;
}

/// Burns CPU. Must NOT sleep: sleeping yields the core, which is exactly what we
/// need MED not to do. Only a genuinely busy thread can preempt.
void busy_for(double ms) {
  const double deadline = now_ms() + ms;
  while (now_ms() < deadline) {
    // spin
  }
}

struct Shared {
  pthread_mutex_t mutex{};
  std::atomic<bool> low_holds_lock{false};
  std::atomic<bool> high_started{false};
  double high_blocked_ms = 0.0;
};

void* low_thread(void* arg) {
  Shared* s = static_cast<Shared*>(arg);
  pthread_mutex_lock(&s->mutex);
  s->low_holds_lock.store(true, std::memory_order_release);

  // The critical section. Under PTHREAD_PRIO_INHERIT the kernel raises this
  // thread to HIGH's priority as soon as HIGH blocks on the mutex, so MED cannot
  // take the CPU away from it here.
  busy_for(kCriticalSectionMs);

  pthread_mutex_unlock(&s->mutex);
  return nullptr;
}

void* high_thread(void* arg) {
  Shared* s = static_cast<Shared*>(arg);
  s->high_started.store(true, std::memory_order_release);

  const double t0 = now_ms();
  pthread_mutex_lock(&s->mutex);  // <-- the blocking we are measuring
  s->high_blocked_ms = now_ms() - t0;
  pthread_mutex_unlock(&s->mutex);
  return nullptr;
}

void* medium_thread(void* /*arg*/) {
  // Never touches the mutex. It is a complete stranger to the resource — and it
  // is still the reason HIGH misses its deadline. That is the whole lesson.
  busy_for(kMediumWorkMs);
  return nullptr;
}

/// Creates a SCHED_FIFO thread pinned to `cpu`. Returns pthread_create's status;
/// EPERM means the process may not use realtime priorities.
int spawn_rt(pthread_t* out, int priority, int cpu, void* (*fn)(void*), void* arg) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  // PTHREAD_EXPLICIT_SCHED is REQUIRED. The default is PTHREAD_INHERIT_SCHED,
  // under which the policy and priority set below are silently IGNORED and the
  // thread inherits the creator's. Forgetting this line is the classic way to
  // "set" a realtime priority that never takes effect.
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

  sched_param param{};
  param.sched_priority = priority;
  pthread_attr_setschedparam(&attr, &param);

  // One CPU for all three threads. With several cores they would simply run in
  // parallel: no preemption, hence no inversion to observe.
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(static_cast<unsigned>(cpu), &set);
  pthread_attr_setaffinity_np(&attr, sizeof(set), &set);

  const int rc = pthread_create(out, &attr, fn, arg);
  pthread_attr_destroy(&attr);
  return rc;
}

}  // namespace

InversionResult run_priority_inversion(bool use_priority_inheritance) {
  InversionResult result;
  result.critical_section_ms = kCriticalSectionMs;
  result.medium_work_ms = kMediumWorkMs;

  Shared shared;

  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  if (use_priority_inheritance) {
    // The one setting JPL had to upload to Mars. The POSIX default is
    // PTHREAD_PRIO_NONE — priority inheritance is OFF unless you ask for it.
    pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
  }
  pthread_mutex_init(&shared.mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);

  // The orchestrating thread stays OFF this CPU so it can supervise freely.
  const int cpu = 0;

  pthread_t low{};
  if (spawn_rt(&low, kPrioLow, cpu, low_thread, &shared) != 0) {
    pthread_mutex_destroy(&shared.mutex);
    return result;  // ran == false: no RT permission
  }

  // Wait until LOW actually owns the mutex, otherwise HIGH might grab it first
  // and there would be nothing to invert.
  while (!shared.low_holds_lock.load(std::memory_order_acquire)) {
    sched_yield();
  }

  pthread_t high{};
  if (spawn_rt(&high, kPrioHigh, cpu, high_thread, &shared) != 0) {
    pthread_join(low, nullptr);
    pthread_mutex_destroy(&shared.mutex);
    return result;
  }

  // Let HIGH reach the mutex and block on it before MED enters the picture.
  while (!shared.high_started.load(std::memory_order_acquire)) {
    sched_yield();
  }
  timespec settle{0, 5 * 1000 * 1000};  // 5 ms
  nanosleep(&settle, nullptr);

  pthread_t med{};
  if (spawn_rt(&med, kPrioMed, cpu, medium_thread, &shared) != 0) {
    pthread_join(low, nullptr);
    pthread_join(high, nullptr);
    pthread_mutex_destroy(&shared.mutex);
    return result;
  }

  pthread_join(low, nullptr);
  pthread_join(high, nullptr);
  pthread_join(med, nullptr);
  pthread_mutex_destroy(&shared.mutex);

  result.ran = true;
  result.high_blocked_ms = shared.high_blocked_ms;
  return result;
}

}  // namespace rt
}  // namespace av
