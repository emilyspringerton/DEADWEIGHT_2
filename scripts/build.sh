#!/usr/bin/env bash
# Clean build + test for DEADWEIGHT_2 Phase D1 (local-only core loop). Usage: scripts/build.sh
# Needs libsdl2-dev (pkg-config sdl2) for the debug shell; the headless core-loop tests have no
# SDL dependency at all. Anything that fails stops the script (set -e), matching DEADWEIGHT's own
# scripts/build.sh convention.
set -euo pipefail
cd "$(dirname "$0")/.."
CFLAGS_BASE="-std=c99 -Wall -Wextra -Werror -Icore"
rm -rf build && mkdir -p build

echo "== C: core-loop tests (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    tests/test_core_loop.c core/items.c core/combat.c core/dummy.c -o build/test_core_loop
./build/test_core_loop

echo "== GUI: local debug shell (SDL2) + headless selftest =="
command -v pkg-config >/dev/null && pkg-config --exists sdl2 || { echo "needs libsdl2-dev (pkg-config sdl2)"; exit 1; }
gcc $CFLAGS_BASE -O2 $(pkg-config --cflags sdl2) \
    apps/local/main.c core/items.c core/combat.c core/dummy.c $(pkg-config --libs sdl2) \
    -o build/dw2_local
./build/dw2_local --selftest

echo "BUILD CLEAN"
