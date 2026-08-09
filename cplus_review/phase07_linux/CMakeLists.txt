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

# W46 -- virtual memory / mmap / page faults. MappedFile is a real RAII component
# (mmap + munmap, move-only) with no std equivalent, so it lives in its own TU.
# The test asserts observable behaviour: zero-copy bytes match the file, touching
# pages raises the minor page-fault count (getrusage), empty file maps to nothing,
# and move-only ownership stays ASAN-clean (no double munmap). See notes/virtual_memory.md.
cr_add_test(phase07_mapped_file_test SOURCES
  tests/mapped_file_test.cpp
  src/mapped_file.cpp)
target_include_directories(phase07_mapped_file_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
