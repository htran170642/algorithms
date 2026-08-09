# Linux & OS (W45-W49)
cr_add_test(phase07_linux_test SOURCES
  tests/placeholder_test.cpp
)
target_include_directories(phase07_linux_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# W45 -- fork/exec/wait + /proc. The spawner is a real component (fork+execve+
# waitpid, pipe'd stdout), so it lives in its own TU under src/. The test only
# asserts observable behaviour: exit codes, WIFSIGNALED vs WIFEXITED, inherited
# fds, and demand paging measured via /proc/self/status (see notes/processes.md).
cr_add_test(phase07_process_spawner_test SOURCES
  tests/process_spawner_test.cpp
  src/process_spawner.cpp)
target_include_directories(phase07_process_spawner_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
