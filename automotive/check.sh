#!/usr/bin/env bash
#
# One command that proves the tree is healthy.
#
#   ./check.sh          debug + asan + ubsan + tsan + clang-tidy
#   ./check.sh fast     debug only (the edit/build loop)
#   ./check.sh tsan     one named preset
#   ./check.sh tidy     static analysis only
#
# CLAUDE.md section 8 requires Debug / Release / ASan / UBSan / TSan builds and
# static analysis. This script is the single entry point for all of them.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

readonly kAllPresets=(debug asan ubsan tsan)

run_preset() {
    local preset="$1"
    echo
    echo "=============================== ${preset} ==============================="
    cmake --preset "${preset}" >/dev/null
    cmake --build --preset "${preset}"
    ctest --preset "${preset}"
}

run_tidy() {
    echo
    echo "=============================== clang-tidy ==============================="
    if ! command -v clang-tidy >/dev/null 2>&1; then
        echo "clang-tidy not installed - skipped (sudo apt install clang-tidy)"
        return 0
    fi

    cmake --preset tidy >/dev/null

    # --clean-first: clang-tidy only runs on translation units that recompile,
    # so an incremental build would silently analyse nothing.
    local output
    output="$(cmake --build --preset tidy --clean-first 2>&1)"

    local warnings
    warnings="$(grep -cE 'warning:' <<<"${output}" || true)"

    if [[ "${warnings}" -gt 0 ]]; then
        grep -E 'warning:' <<<"${output}" | sort -u
        echo
        echo "clang-tidy: ${warnings} warning(s)"
        return 1
    fi

    echo "clang-tidy: clean"
}

main() {
    local target="${1:-all}"

    case "${target}" in
        all)
            for preset in "${kAllPresets[@]}"; do
                run_preset "${preset}"
            done
            run_tidy
            ;;
        fast)
            run_preset debug
            ;;
        tidy)
            run_tidy
            ;;
        debug | release | asan | ubsan | tsan)
            run_preset "${target}"
            ;;
        *)
            echo "usage: $0 [all|fast|tidy|debug|release|asan|ubsan|tsan]" >&2
            return 2
            ;;
    esac

    echo
    echo "OK"
}

main "$@"
