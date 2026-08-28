# Week 3 — Linux IPC: epoll, Unix sockets, shared memory

## 1. Concept

Week 2 moved data between **threads** (one address space, a mutex is enough).
Week 3 moves it between **processes**, and that changes what is possible:

| | threads | processes |
|---|---|---|
| Share a pointer | yes | **no** — an address means nothing in the peer |
| Synchronise with `std::mutex` | yes | only if the mutex itself lives in shared memory |
| One crashes | takes the other down | the other survives |
| Cost of a hand-off | a cache line | a syscall, or a mapped page |

That third row is the reason a cockpit uses processes at all. If the IVI
renderer segfaults, the cluster must keep showing speed. Threads cannot give
you that; separate address spaces can. This is the same argument the hypervisor
makes one level up in week 13 — freedom from interference, at a different
granularity.

Three mechanisms, three different jobs:

- **`epoll`** — one thread, many descriptors. Register interest once, block
  until *something* is ready. Replaces "a thread per fd" (expensive, and now
  everything needs locking) and "poll in a loop" (burns a core to learn nothing
  happened).
- **Unix domain socket** — the kernel does the copying and the synchronising.
  Costs two copies and two syscalls per message. In exchange you get
  connection semantics, peer-death detection, and no shared state to corrupt.
- **Shared memory** — both processes map the same physical pages. One copy, no
  syscall. In exchange you write the synchronisation yourself, with atomics,
  and a crash mid-update leaves the other side looking at half-written data.

## 2. Why it exists in a Cockpit DC

The architecture in CLAUDE.md has the cluster (safety-relevant, often QNX) and
the IVI (Android, huge, third-party code) as separate domains. Between them,
vehicle signals still have to flow. Which transport you pick is a safety
argument, not a performance one:

- A **socket** is the safe default. Nothing shared means nothing corruptible,
  and `EPIPE` / `EPOLLRDHUP` tell you the peer died. Use it for control,
  configuration, request/response, anything low rate.
- **Shared memory** is for the hot path — a video frame, a point cloud, a
  100 Hz signal set — where 10x latency and 10x CPU actually matter. Every use
  needs an answer to "what does the reader see if the writer dies mid-write?"

The measurement below is what makes that a decision rather than a preference.

## 3. Architecture

```text
             cluster process                    IVI process
        +----------------------+          +---------------------+
        |   epoll (one thread) |          |                     |
        |     |            |   |          |                     |
        |  ctrl fd      data   |          |                     |
        +-----|------------|---+          +---------------------+
              |            |                         |
              |            +----- shared memory -----+   hot path
              |                    (SharedRing)          1 copy, 0 syscalls
              |
              +------------------ unix socket ---------+   control path
                                  (SOCK_SEQPACKET)         2 copies, 2 syscalls
```

Both directions come back together in week 8, where the middleware picks per
signal rather than per process.

## 4. C++ / Linux implementation

| File | Role |
|---|---|
| [libs/av_ipc/include/av/ipc/unique_fd.hpp](../libs/av_ipc/include/av/ipc/unique_fd.hpp) | RAII for a descriptor — the Rule of 5 written once |
| [libs/av_ipc/include/av/ipc/unix_socket.hpp](../libs/av_ipc/include/av/ipc/unix_socket.hpp) · [.cpp](../libs/av_ipc/src/unix_socket.cpp) | `SOCK_SEQPACKET` endpoint, `IoStatus`, non-blocking mode |
| [libs/av_ipc/include/av/ipc/poller.hpp](../libs/av_ipc/include/av/ipc/poller.hpp) · [.cpp](../libs/av_ipc/src/poller.cpp) | epoll wrapper, level-triggered, `EPOLLRDHUP` |
| [libs/av_ipc/include/av/ipc/shared_ring.hpp](../libs/av_ipc/include/av/ipc/shared_ring.hpp) · [.cpp](../libs/av_ipc/src/shared_ring.cpp) | `shm_open` + `mmap`, SPSC lock-free ring |
| [apps/ipc_bench/main.cpp](../apps/ipc_bench/main.cpp) | the measurement, across a real `fork()` |

### Decisions worth defending

**`SOCK_SEQPACKET`, not `SOCK_STREAM`.** SEQPACKET is the only AF_UNIX type
that is reliable, ordered, *and* preserves message boundaries. A `CanFrame`
sent as one message arrives as one message. Over STREAM you must invent a
length prefix and reassemble partial reads — the single largest source of bugs
when people reach for STREAM out of habit. `PreservesMessageBoundaries` in the
tests is the proof.

**`MSG_NOSIGNAL` on every send.** Without it, writing to a socket whose peer
has gone raises `SIGPIPE`, whose default disposition is to kill the process.
A cockpit must learn about a dead peer as a return code.

**`IoStatus::WouldBlock` is not an error.** On a non-blocking fd it is the
normal answer meaning "come back when epoll says so". Folding it into `Error`
is how a poll loop becomes a busy-wait or a spurious shutdown.

**Level-triggered epoll, not edge-triggered.** Edge-triggered is faster but
requires draining every fd to `EAGAIN` on every wakeup; miss it once and that
fd goes silent forever. Level-triggered forgives a partial read. Optimise when
a profile says so, not before.

**`EPOLLRDHUP` always.** Without it a dead peer is only noticed when somebody
happens to read and gets zero bytes. With it, the disconnect is an event.

**The listener unlinks its own socket file.** Closing a socket does not remove
its path. Only the process that created it may unlink it — a client that
unlinked would delete a live endpoint. Same rule for `shm_unlink`.

**Ring counters are monotonic `uint64_t`, not indices.** `tail - head` is the
count and needs no separate "full or empty?" flag. At 1 MHz it would take
580,000 years to wrap.

**`head` and `tail` on separate cache lines.** Producer and consumer each write
one. On the same line, every push invalidates the consumer's copy and every pop
invalidates the producer's — false sharing, the classic way a lock-free
structure ends up slower than a mutex.

### The memory ordering, in full

This is the part worth being able to reconstruct from scratch:

```cpp
// producer
tail = tail_.load(relaxed);      // only this thread writes tail
head = head_.load(acquire);      // pairs with the consumer's release
if (tail - head >= capacity) return false;
memcpy(slot(tail % capacity), &value, sizeof(T));
tail_.store(tail + 1, release);  // publishes the slot write
```

- `relaxed` on its own counter: a thread cannot fail to observe its own store.
- `acquire` on the peer's counter: everything the peer did before its release
  store is visible to us afterwards.
- `release` on the publish: the `memcpy` **must not** be reordered after it, or
  the consumer can read a slot that was never written.

Drop the `release` and the code still passes every test on x86, because x86
does not reorder stores. It fails on ARM, which is what a real cockpit SoC is.
That is the reason to reason about this rather than test it.

## 5. Hands-on exercise

```bash
./check.sh                             # 7 test binaries, all 4 sanitizers
./build/debug/apps/ipc_bench/ipc_bench
```

Measured on this machine (round-trip, two processes, 32-byte payload, 20 000
samples after 1 000 warm-up):

```text
  transport                    median          p99          max
  unix socket (RTT)          18.72 us     27.66 us    646.76 us
  shared memory (RTT)         1.89 us      2.38 us     29.61 us
```

**~10x at the median, ~12x at p99.** Read the `max` column too: the socket's
worst case is 646 us, which is the scheduler putting the process to sleep and
waking it later. That tail — not the median — is what makes a signal miss its
frame.

The number that is *not* in the table: the shared-memory child spins. It burns
a full core to get those 1.89 us. A real design pairs the ring with an
`eventfd` so the consumer can sleep when the ring is empty, trading a few us
back for the CPU.

Things to try:

1. Grow the payload to 4 KB. The socket gap narrows — copies start to dominate
   the syscall. The choice depends on message size, not just rate.
2. Delete `release` from `try_push` and rerun the tests. Everything still
   passes on x86. That is the lesson.
3. Run `ipc_bench` under `taskset -c 0` so both processes share one core. The
   spin-waiting ring collapses; the socket barely notices, because blocking
   lets the peer run.

## 6. Failure / debugging exercise

| Fault injected | Detected by | Logged as | Fallback | Safe state |
|---|---|---|---|---|
| Peer process exits | `EPOLLRDHUP` / `recv` returns `PeerClosed` | `ERROR ipc.socket` | drop the connection, keep serving others | reconnect loop (week 8) |
| Stale socket file after a crash | `bind` would give `EADDRINUSE` | `WARN could not remove stale endpoint` | unlink and rebind | restart always succeeds |
| Path longer than `sun_path` (108) | length check before the copy | `ERROR path rejected` | refuse to listen | never binds the wrong path |
| Two creators of one shm name | `O_EXCL` on `shm_open` | `ERROR shm_open create failed` | second one refuses | indices cannot be corrupted |
| Peer disagrees about `sizeof(T)` | `element_size` check in `open()` | — | refuse to attach | never reads garbage slots |
| `mmap` before `ftruncate` | **nothing** — `SIGBUS` on first touch | nothing | none | why `ftruncate` comes first |
| Missing `release` store | **nothing on x86** | nothing | none | fails only on ARM, in the field |

The last two rows are week 3's contribution to the pattern. Week 1 had a fault
that produced a plausible wrong number; week 2 had one that produced no output
at all. Here we have a fault that produces **correct behaviour on the
development machine and wrong behaviour on the target**. No amount of testing
on x86 finds it — only reading the code with the memory model in mind does.

## 7. Senior interview questions

**1. Why `SOCK_SEQPACKET` and not `SOCK_STREAM`?**

Message boundaries. STREAM is a byte pipe: three sends of 3, 2 and 4 bytes can
arrive as one 9-byte read, so the receiver needs its own framing — a length
prefix, and a reassembly buffer for partial reads. SEQPACKET keeps the
boundaries and is still reliable and ordered. DGRAM keeps boundaries too, but
is connectionless, so you lose peer-death detection.

**2. When would you *not* use shared memory, given it is 10x faster?**

When the payload is small and infrequent, because 17 us saved on a 10 Hz signal
is noise. When either side is untrusted or crash-prone, because a writer that
dies mid-update leaves the reader looking at a torn structure and there is no
kernel to clean up. When the two sides have different lifetimes, because
attaching to a region whose creator is gone is a whole protocol of its own.
Sockets fail cleanly; shared memory fails silently.

**3. Level-triggered or edge-triggered epoll?**

Level-triggered unless measured otherwise. Edge-triggered reports a transition,
so you must drain each fd until `EAGAIN` on every wakeup — miss one byte and
that fd never reports again, and the bug looks like "one client randomly hangs".
Level-triggered reports the state, so a partial read is simply reported again.

**4. What does `EAGAIN` mean on a non-blocking read, and what should the code do?**

Nothing is available right now. It is the normal steady state of an idle
socket, not an error. The code must return to `epoll_wait`, not retry
immediately (that is a busy-wait) and not tear the connection down (that is a
spurious shutdown). `EINTR` is the other one people get wrong: a signal arrived
while blocked, and the correct response is to call again.

**5. Explain the acquire/release pair in the ring.**

The producer's `release` store to `tail` publishes everything it wrote before
it, including the copy into the slot. The consumer's `acquire` load of `tail`
makes those writes visible to it. Without the pair, the compiler or the CPU may
move the slot write after the counter update, and the consumer reads a slot the
producer has not filled. On x86 the store ordering hides the bug; on ARM it
does not.

**6. Why do `head` and `tail` need to be on different cache lines?**

Because the producer writes one and the consumer writes the other. On the same
64-byte line, each write invalidates the other core's copy, so every operation
becomes a cache-line transfer between cores — false sharing. The structure is
lock-free and still slower than a mutex.

**7. A process crashed and now the service will not start: `bind` gives
`EADDRINUSE`. Why, and what is the fix?**

An AF_UNIX socket file outlives the process that created it; closing the fd
does not unlink the path. The fix is to `unlink` the stale path before `bind`,
treating `ENOENT` as the normal case. The same applies to `shm_unlink` for
POSIX shared memory — otherwise `/dev/shm` accumulates dead regions until the
tmpfs fills.

## 8. Review checklist

- [x] I can explain this without notes
- [x] I implemented it, not just read it
- [x] I broke it on purpose and watched it fail — stale socket file, duplicate
      shm creator, mismatched element size, truncated SEQPACKET message
- [x] I can defend the design trade-off I chose — SEQPACKET over STREAM,
      level-triggered over edge-triggered, socket for control and shm for the
      hot path
- [x] Tests pass under `./check.sh` — debug / asan / ubsan / **tsan** / tidy clean

### Left for later, deliberately

- **`eventfd` alongside the ring** -> week 8, so the consumer can sleep instead
  of spinning. This is the honest fix for the CPU cost measured above.
- **Torn-read protection** (a sequence lock, or a generation counter per slot)
  -> week 15, with the rest of the fault injection. Right now a writer that dies
  mid-copy leaves a half-written slot and nothing detects it.
- **Reading `can-utils` source** (`candump.c`, `cansend.c`) -> week 5, where the
  same `epoll` loop gets a real `PF_CAN` socket instead of a simulated one.
- **CPU affinity and scheduling priority** -> week 12, with the Qt thread model,
  where priority inversion actually becomes reachable.
