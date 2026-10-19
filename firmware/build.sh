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

if [[ ! -d components/gen_bmgr_codes ]]; then
    idf.py gen-bmgr-config -c ./boards -b "$BOARD"
fi

if [[ ! -f sdkconfig ]]; then
    idf.py set-target esp32p4
fi

idf.py build

if [[ "${1:-}" == "flash" ]]; then
    : "${2:?usage: ./build.sh flash PORT}"
    idf.py -p "$2" flash monitor
fi
