#!/usr/bin/env bash
# Builds and runs the host tests (no ESP32 needed).
# Needs g++ on PATH (scoop install mingw on Windows). Run from anywhere:
#     bash test/run_tests.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/test/build"
mkdir -p "$OUT"

#firmware sources under test, compiled against the fakes in test/fakes
SOURCES=(
    "$ROOT/src/core/func_gen.cpp"
    "$ROOT/src/core/funcgen_controller.cpp"
    "$ROOT/src/libs/AD9833/AD9833.cpp"
    "$ROOT/src/libs/MCP4131/MCP4131.cpp"
)

g++ -std=gnu++17 -Wall -Wextra -g -O0 \
    -I"$ROOT/test/fakes" \
    "$ROOT"/test/*.cpp "$ROOT/test/fakes/fakes.cpp" "${SOURCES[@]}" \
    -o "$OUT/host_tests"

"$OUT/host_tests"
