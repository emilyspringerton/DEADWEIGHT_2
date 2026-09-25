#!/usr/bin/env bash
# Clean build + test for DEADWEIGHT_2. Usage: scripts/build.sh
# Needs libsdl2-dev (pkg-config sdl2) for the debug shell; the headless core-loop tests and
# dw2_server have no SDL dependency at all. Anything that fails stops the script (set -e),
# matching DEADWEIGHT's own scripts/build.sh convention.
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

echo "== D2: dw2_server + headless wire-protocol smoke test (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iapps/server \
    core/items.c core/combat.c core/protocol.c core/http.c core/iduna.c apps/server/main.c \
    -o build/dw2_server -lpthread -lm
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    core/protocol.c tools/dw2_test_client.c -o build/dw2_test_client

DW2_TEST_PORT=17800
./build/dw2_server --port "$DW2_TEST_PORT" --no-auth --fast-forward --pack-ms 3000 --tick-ms 20 --verbose \
    > build/dw2_server_smoketest.log 2>&1 &
DW2_SRV_PID=$!
trap 'kill "$DW2_SRV_PID" 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do ss -ltn 2>/dev/null | grep -q ":$DW2_TEST_PORT " && break; sleep 0.1; done

# Same real loadout apps/local's own --selftest fights (Generator+Conductor+Railgun+Bulwark) vs an
# empty grid -- proves a real, full server-authoritative match resolves through the actual TCP wire
# protocol (not a shortcut), same "run it for real" discipline --selftest already established for
# the local core loop.
./build/dw2_test_client --port "$DW2_TEST_PORT" --name SmokeA --timeout-ms 15000 \
    --place 0,0,0,0 --place 1,0,1,0 --place 3,0,2,0 --place 4,3,3,0 > build/dw2_smoke_a.log &
CLIENT_A_PID=$!
./build/dw2_test_client --port "$DW2_TEST_PORT" --name SmokeB --timeout-ms 15000 > build/dw2_smoke_b.log &
CLIENT_B_PID=$!
wait "$CLIENT_A_PID"; wait "$CLIENT_B_PID"
kill "$DW2_SRV_PID" 2>/dev/null || true; trap - EXIT

grep -q "^MATCH_END result=1 reason=0" build/dw2_smoke_a.log || { echo "smoke test FAILED: expected loadout (A) to win on hull, see build/dw2_smoke_a.log"; cat build/dw2_smoke_a.log; exit 1; }
grep -q "^MATCH_END result=0 reason=0" build/dw2_smoke_b.log || { echo "smoke test FAILED: expected empty grid (B) to lose on hull, see build/dw2_smoke_b.log"; cat build/dw2_smoke_b.log; exit 1; }
echo "dw2_server smoke test: A (real loadout) beat B (empty grid) on hull, as hand-derived"

echo "BUILD CLEAN"
