# W2 — RT-safe concurrency (throwaway spike, not product).
#
# Credit from cplus_review W32-W38: thread, mutex, condition_variable, atomics,
# memory_order, ThreadPool, BlockingQueue, SpscRing. This week covers only the
# automotive delta: priority inversion and priority inheritance.
#
# pthreads directly rather than std::thread — the scheduling policy has to be set
# in the thread attributes at creation, which std::thread does not expose.

add_library(phase01_priority_inversion STATIC src/priority_inversion.cpp)
target_include_directories(phase01_priority_inversion
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(phase01_priority_inversion PUBLIC av_base Threads::Threads)

av_add_test(phase01_priority_inversion_test
  SOURCES tests/priority_inversion_test.cpp
  LIBS    phase01_priority_inversion)
