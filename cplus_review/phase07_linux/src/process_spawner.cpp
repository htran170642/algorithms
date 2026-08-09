#include "process_spawner.hpp"

#include <sys/wait.h>  // waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <unistd.h>    // fork, execv, dup2, close, read, pipe, _exit, STDOUT_FILENO

#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace cr {

namespace {

// Turn {path, args...} into the NULL-terminated char* argv[] execv wants.
// Built in the PARENT before fork() so the child does the bare minimum
// (dup2/close/execv/_exit) -- the less we do post-fork, the fewer async-signal
// hazards. `storage` owns the bytes; `ptrs` points into it.
struct Argv {
    std::vector<std::string> storage;
    std::vector<char*>       ptrs;
};

Argv make_argv(const std::string& path, const std::vector<std::string>& args) {
    Argv a;
    a.storage.reserve(args.size() + 1);
    a.storage.push_back(path);
    for (const auto& s : args) {
        a.storage.push_back(s);
    }
    a.ptrs.reserve(a.storage.size() + 1);
    for (auto& s : a.storage) {
        a.ptrs.push_back(s.data());  // std::string::data() is char* since C++17
    }
    a.ptrs.push_back(nullptr);
    return a;
}

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

// Drain a read-end fd to EOF, retrying across EINTR. Caller closes the fd.
std::string drain(int fd) {
    std::string out;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
            break;  // EOF: all writers closed
        } else if (errno == EINTR) {
            continue;  // interrupted by a signal, retry
        } else {
            break;  // genuine read error; return what we have
        }
    }
    return out;
}

}  // namespace

ProcessResult ProcessSpawner::run(const std::string& path,
                                  const std::vector<std::string>& args,
                                  bool capture_stdout) {
    Argv argv = make_argv(path, args);

    int fds[2] = {-1, -1};  // fds[0] = read end, fds[1] = write end
    if (capture_stdout && ::pipe(fds) == -1) {
        throw_errno("pipe");
    }

    const pid_t pid = ::fork();
    if (pid == -1) {
        if (capture_stdout) {
            ::close(fds[0]);
            ::close(fds[1]);
        }
        throw_errno("fork");
    }

    if (pid == 0) {
        // ===== CHILD =====
        if (capture_stdout) {
            ::dup2(fds[1], STDOUT_FILENO);  // stdout -> pipe write end
            ::close(fds[0]);                // originals no longer needed
            ::close(fds[1]);
        }
        ::execv(path.c_str(), argv.ptrs.data());
        // Only reached if execv failed. Must _exit (not exit/return): we must not
        // flush the parent's stdio buffers or unwind back into parent code.
        ::_exit(127);
    }

    // ===== PARENT =====
    ProcessResult result;
    if (capture_stdout) {
        ::close(fds[1]);              // close write end so drain() sees EOF
        result.output = drain(fds[0]);
        ::close(fds[0]);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited == -1) {
        throw_errno("waitpid");
    }

    if (WIFEXITED(status)) {
        result.exited_normally = true;
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exited_normally = false;
        result.term_signal = WTERMSIG(status);
    }
    return result;
}

long read_vm_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            // Format: "VmRSS:\t   12345 kB"
            std::istringstream iss(line.substr(6));
            long kb = 0;
            std::string unit;
            if (iss >> kb >> unit) {
                return kb;
            }
            return 0;
        }
    }
    return 0;
}

}  // namespace cr
