#!/usr/bin/env bash
# Build the Tab5 firmware with the project's pinned toolchain (ESP-IDF v5.5.3, see ADR-0004).
#
# Usage:
#   ./build.sh                 configure (if needed) and build
#   ./build.sh flash PORT      build, then flash and open the monitor on PORT
#   ./build.sh clean           remove build/, sdkconfig and generated board code
#
# Override the IDF location with IDF_EXPORT=/path/to/export.sh
set -euo pipefail

cd "$(dirname "$0")"

IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf-v5.5.3/export.sh}"
BOARD="m5stack_tab5"

if [[ ! -f "$IDF_EXPORT" ]]; then
    echo "ESP-IDF export script not found: $IDF_EXPORT" >&2
    echo "Install ESP-IDF v5.5.3 or set IDF_EXPORT." >&2
    exit 1
fi
# shellcheck disable=SC1090
source "$IDF_EXPORT" >/dev/null

case "${1:-build}" in
    clean)
        rm -rf build sdkconfig sdkconfig.old components/gen_bmgr_codes
        echo "cleaned"
        exit 0
        ;;
esac

# Regenerate the board code when it is missing or any board file changed since it was generated;
# otherwise YAML edits are silently ignored.
GEN_STAMP=components/gen_bmgr_codes/gen_board_device_config.c
if [[ ! -f "$GEN_STAMP" ]] || [[ -n "$(find "boards/$BOARD" -type f -newer "$GEN_STAMP" -print -quit)" ]]; then
    # gen-bmgr-config moves sdkconfig away and drops build/CMakeCache.txt, so set-target runs below
    idf.py gen-bmgr-config -c ./boards -b "$BOARD"
fi

if [[ ! -f sdkconfig ]]; then
    # set-target's fullclean refuses a build directory without CMakeCache.txt, so start from scratch
    rm -rf build
    idf.py set-target esp32p4
fi

idf.py build

if [[ "${1:-}" == "flash" ]]; then
    : "${2:?usage: ./build.sh flash PORT}"
    idf.py -p "$2" flash monitor
fi
