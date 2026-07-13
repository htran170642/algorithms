# The Week 0 proof-of-substrate target.
#
# NOT registered with add_test() — it is SUPPOSED to fail. Its job is to make the
# sanitizers fire once, in front of you, so you know they're really connected.
#
# Warnings are relaxed here only because the code is deliberately wrong; -Werror
# would (correctly) reject it. This is the one target in the repo allowed to be bad.

add_executable(scratch_ub scratch.cpp)
target_compile_options(scratch_ub PRIVATE -Wall -Wno-error)
