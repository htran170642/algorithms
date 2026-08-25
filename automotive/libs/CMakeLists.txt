# Reusable middleware — the code the CDC Simulator actually links.
#
# Everything here is hardened, tested under both standards and every sanitizer, and
# subject to av_enable_strict_warnings(). Throwaway learning spikes belong in
# phases/, not here.

add_subdirectory(av_core)      # W1, W3: fixed containers, bit, endian, mmio, clock
# add_subdirectory(av_signal)  # W4: the vehicle signal model
# add_subdirectory(av_ipc)     # W4: UDS, shm, eventfd
# add_subdirectory(av_can)     # W8-9
# add_subdirectory(av_someip)  # W10
# add_subdirectory(av_uds)     # W11
