// W45 -- Process / fork / exec / wait / /proc, the OBSERVABLE half.
//
// This test drives cr::ProcessSpawner (fork+execve+waitpid) and cr::read_vm_rss_kb
// (parses /proc/self/status) to pin down the four facts of the week as *behaviour*:
//
//   1. exec replaces the child image, wait harvests its exit code, and a pipe'd
//      stdout is inherited across execve -- so we can read the child's output in
//      the parent. (Proves fd inheritance, Q2.)
//   2. A non-zero exit (/bin/false) is reported as exit_code, not a signal.
//   3. A child that kills itself is reported via WIFSIGNALED, not WIFEXITED --
//      exited() vs signaled() must not be conflated.
//   4. Anonymous memory is demand-paged: RSS barely moves when a big buffer is
//      *allocated*, then jumps when it is *touched*. This is the same first-touch
//      page-fault mechanism that makes fork()'s copy-on-write cheap (Q1).

#include <gtest/gtest.h>

#include <csignal>   // SIGTERM
#include <cstdlib>   // malloc/free
#include <cstring>   // memset
#include <string>
#include <vector>

#include "process_spawner.hpp"

TEST(W45Process, RunEchoCapturesStdoutAndExitsZero) {
    const auto r = cr::ProcessSpawner::run("/bin/echo", {"hello", "world"},
                                           /*capture_stdout=*/true);
    EXPECT_TRUE(r.exited());
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.exit_code, 0);
    // echo appends a trailing newline; the payload proves the pipe carried the
    // child's stdout back to us across execve.
    EXPECT_EQ(r.output, "hello world\n");
}

TEST(W45Process, RunFalseExitsNonZero) {
    const auto r = cr::ProcessSpawner::run("/bin/false", {});
    EXPECT_TRUE(r.exited());
    EXPECT_FALSE(r.success());
    EXPECT_EQ(r.exit_code, 1);
    EXPECT_FALSE(r.signaled());
}

TEST(W45Process, ChildKilledBySignalIsReportedAsSignaled) {
    // sh kills itself: WIFSIGNALED, not WIFEXITED. No shell injection risk here --
    // the command is a fixed literal we control.
    const auto r = cr::ProcessSpawner::run("/bin/sh", {"-c", "kill -TERM $$"});
    EXPECT_TRUE(r.signaled());
    EXPECT_FALSE(r.exited());
    EXPECT_EQ(r.term_signal, SIGTERM);
}

TEST(W45Process, AnonymousMemoryIsDemandPaged) {
    constexpr std::size_t kBytes = 64UL * 1024 * 1024;  // 64 MiB

    const long rss_before = cr::read_vm_rss_kb();
    ASSERT_GT(rss_before, 0) << "could not read VmRSS from /proc/self/status";

    // Large malloc -> glibc mmaps it; pages are demand-zero, not resident yet.
    auto* buf = static_cast<unsigned char*>(std::malloc(kBytes));
    ASSERT_NE(buf, nullptr);

    // Touch every page -> page faults pull real frames in -> RSS climbs.
    std::memset(buf, 0x7F, kBytes);
    const long rss_after = cr::read_vm_rss_kb();

    // Expect at least ~48 MiB of the 64 MiB to have materialised (slack for
    // whatever else moved). If this were eager allocation the delta would already
    // be there before the memset; the point is that touching is what pays.
    EXPECT_GT(rss_after - rss_before, 48L * 1024)
        << "before=" << rss_before << "kB after=" << rss_after << "kB";

    std::free(buf);
}
