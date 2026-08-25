# Weekly study spikes — throwaway, NOT product.
#
# These exist to SEE the machinery once (a priority-inversion demo, a volatile
# elision probe, a deliberate MISRA violation gallery). They are not linked by any
# app and are exempt from the strict warning tier, because several of them are
# supposed to be wrong.

add_subdirectory(phase01_rt_concurrency)   # W2: priority inversion, measured
add_subdirectory(phase02_embedded)         # W3: volatile, proven from assembly
