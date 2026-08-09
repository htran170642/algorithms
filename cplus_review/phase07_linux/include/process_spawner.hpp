#pragma once

#include <string>
#include <vector>

namespace cr {

// Result of a child process that has already been reaped by waitpid().
//
// exited() and signaled() are mutually exclusive: a process either ran to
// completion (WIFEXITED -> exit_code is meaningful) or was terminated by a
// signal (WIFSIGNALED -> term_signal is meaningful). Conflating the two is the
// classic bug this type exists to prevent.
struct ProcessResult {
    bool exited_normally = false;  // set from WIFEXITED
    int  exit_code       = -1;     // WEXITSTATUS -- only meaningful if exited()
    int  term_signal     = 0;      // WTERMSIG   -- only meaningful if signaled()
    std::string output;            // child's stdout, iff capture_stdout was set

    [[nodiscard]] bool exited()   const noexcept { return exited_normally; }
    [[nodiscard]] bool signaled() const noexcept { return !exited_normally; }
    [[nodiscard]] bool success()  const noexcept {
        return exited_normally && exit_code == 0;
    }
};

class ProcessSpawner {
public:
    // fork() + execv(path, {path, args..., nullptr}) + waitpid().
    //
    // execv does NOT go through a shell, so `args` are passed verbatim -- there
    // is no word-splitting or globbing and therefore no shell-injection surface.
    // When capture_stdout is true the child's stdout is redirected through a pipe
    // and drained into ProcessResult::output.
    //
    // Throws std::system_error if fork()/pipe() fail. An execv() failure is not
    // thrown from here (it happens in the child); it surfaces as exit_code 127.
    static ProcessResult run(const std::string& path,
                             const std::vector<std::string>& args,
                             bool capture_stdout = false);
};

// VmRSS (resident set size, in kB) of the current process, read from
// /proc/self/status. Returns 0 if the field cannot be read/parsed.
[[nodiscard]] long read_vm_rss_kb();

}  // namespace cr
