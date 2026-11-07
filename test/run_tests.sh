#!/usr/bin/env bash
# Builds and runs the host tests (no ESP32 needed).
# Needs g++ on PATH (scoop install mingw on Windows). Run from anywhere:
#     bash test/run_tests.sh
# The dashboard test also saves screenshots to test/build/screens.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/test/build"
mkdir -p "$OUT/screens"

#firmware sources under test, compiled against the fakes in test/fakes
SOURCES=(
    "$ROOT/src/core/func_gen.cpp"
    "$ROOT/src/core/funcgen_controller.cpp"
    "$ROOT/src/ui/input_logic.cpp"
    "$ROOT/src/ui/dashboard_logic.cpp"
    "$ROOT/src/ui/dashboard_view.cpp"
    "$ROOT/src/libs/AD9833/AD9833.cpp"
    "$ROOT/src/libs/MCP4131/MCP4131.cpp"
    "$ROOT/src/libs/Adafruit_GFX/Adafruit_GFX.cpp"
)

#ARDUINO is checked by Adafruit_GFX.h before it includes Arduino.h, so it has to be set here
g++ -std=gnu++17 -Wall -Wextra -g -O0 \
    -DARDUINO=10607 -DSCREENS_DIR="\"$OUT/screens\"" \
    -I"$ROOT/test/fakes" \
    "$ROOT"/test/*.cpp "$ROOT/test/fakes/fakes.cpp" "${SOURCES[@]}" \
    -o "$OUT/host_tests"

"$OUT/host_tests"
