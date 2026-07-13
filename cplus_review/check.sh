#!/usr/bin/env bash
# The gate. What CI would run, and what "a week is done" actually means.
#
#   ./check.sh          # both standards, debug + asan + ubsan
#   ./check.sh --tsan   # add ThreadSanitizer (Phase 5 onward)
#   ./check.sh --tidy   # add the clang-tidy Coding Style gate
#   ./check.sh --all    # everything
#
# The dual C++20/C++23 matrix is the point, not overhead: it's what builds the
# instinct for "is this feature available in the codebase I'm interviewing
# against?" Most production C++ is C++20 or older.

set -uo pipefail
cd "$(dirname "$0")"

CONFIGS=(debug asan ubsan)
RUN_TIDY=0

for arg in "$@"; do
  case "$arg" in
    --tsan) CONFIGS+=(tsan) ;;
    --tidy) RUN_TIDY=1 ;;
    --all)  CONFIGS+=(tsan); RUN_TIDY=1 ;;
    *) echo "unknown flag: $arg"; exit 2 ;;
  esac
done

FAILED=()
pass() { printf '  \033[32m✓\033[0m %s\n' "$1"; }
fail() { printf '  \033[31m✗\033[0m %s\n' "$1"; FAILED+=("$1"); }

for std in 20 23; do
  for cfg in "${CONFIGS[@]}"; do
    preset="${cfg}-${std}"
    printf '\n\033[1m== %s ==\033[0m\n' "$preset"

    if ! cmake --preset "$preset" >/dev/null 2>&1; then
      fail "$preset (configure)"; continue
    fi
    if ! cmake --build --preset "$preset" >/dev/null 2>&1; then
      fail "$preset (build)"
      cmake --build --preset "$preset" 2>&1 | tail -25
      continue
    fi
    if ctest --preset "$preset" >/dev/null 2>&1; then
      pass "$preset"
    else
      fail "$preset (tests)"
      ctest --preset "$preset" --output-on-failure 2>&1 | tail -25
    fi
  done
done

# --- Coding Style gate (mechanizes CLAUDE.md, see .clang-tidy) ---------------
if [[ $RUN_TIDY -eq 1 ]]; then
  printf '\n\033[1m== clang-tidy ==\033[0m\n'
  if ! command -v clang-tidy >/dev/null 2>&1; then
    printf '  \033[33m!\033[0m clang-tidy not installed — skipping (sudo apt install clang-tidy)\n'
  else
    mapfile -t SRCS < <(git ls-files '*.cpp' '*.hpp' | grep -v '^scratch/')
    if [[ ${#SRCS[@]} -eq 0 ]]; then
      printf '  (no tracked sources yet)\n'
    elif clang-tidy -p build/debug-23 --quiet "${SRCS[@]}" 2>/dev/null; then
      pass "clang-tidy"
    else
      fail "clang-tidy"
    fi
  fi
fi

printf '\n'
if [[ ${#FAILED[@]} -eq 0 ]]; then
  printf '\033[32mALL GREEN\033[0m — C++20 and C++23, every sanitizer.\n'
  exit 0
fi
printf '\033[31mFAILED:\033[0m %s\n' "${FAILED[*]}"
exit 1
