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
    tests/test_core_loop.c core/items.c core/combat.c core/dummy.c core/round.c -o build/test_core_loop
./build/test_core_loop

echo "== GUI: local debug shell (SDL2) + headless selftest =="
command -v pkg-config >/dev/null && pkg-config --exists sdl2 || { echo "needs libsdl2-dev (pkg-config sdl2)"; exit 1; }
gcc $CFLAGS_BASE -O2 $(pkg-config --cflags sdl2) \
    apps/local/main.c core/items.c core/combat.c core/dummy.c $(pkg-config --libs sdl2) \
    -o build/dw2_local
./build/dw2_local --selftest

echo "== D2: dw2_server + headless wire-protocol smoke test (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all -Iapps/server \
    core/items.c core/combat.c core/round.c core/protocol.c core/http.c core/iduna.c apps/server/main.c \
    -o build/dw2_server -lpthread -lm
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    core/protocol.c core/round.c core/combat.c core/items.c tools/dw2_test_client.c -o build/dw2_test_client

DW2_TEST_PORT=17800
./build/dw2_server --port "$DW2_TEST_PORT" --no-auth --fast-forward --pack-ms 3000 --tick-ms 20 --verbose \
    > build/dw2_server_smoketest.log 2>&1 &
DW2_SRV_PID=$!
trap 'kill "$DW2_SRV_PID" 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do ss -ltn 2>/dev/null | grep -q ":$DW2_TEST_PORT " && break; sleep 0.1; done

# Same real loadout apps/local's own --selftest fights (Generator+Conductor+Railgun+Bulwark) vs an
# empty grid -- proves a real, full server-authoritative match resolves through the actual TCP wire
# protocol (not a shortcut), same "run it for real" discipline --selftest already established for
# the local core loop. Neither client scripts a --round-call, so every round-break (core/round.h,
# EMILY/BACKLOG.md SECTION 548) now happening mid-match grades DW2_GRADE_NONE for both sides --
# fully neutral (dmg_mult stays 1.0 throughout) -- so the ORIGINAL hand-derived 21-tick result is
# byte-identical to before the round-break redesign; --timeout-ms is bumped (15000 -> 20000) purely
# because each of the 4 round-breaks before tick 21 now adds a real ~1200ms wall-clock wait with no
# caller (DW2_ROUND_BUDGET_MS), not because the combat math itself changed.
./build/dw2_test_client --port "$DW2_TEST_PORT" --name SmokeA --timeout-ms 20000 \
    --place 0,0,0,0 --place 1,0,1,0 --place 3,0,2,0 --place 4,3,3,0 > build/dw2_smoke_a.log &
CLIENT_A_PID=$!
./build/dw2_test_client --port "$DW2_TEST_PORT" --name SmokeB --timeout-ms 20000 > build/dw2_smoke_b.log &
CLIENT_B_PID=$!
wait "$CLIENT_A_PID"; wait "$CLIENT_B_PID"
kill "$DW2_SRV_PID" 2>/dev/null || true; trap - EXIT

grep -q "^MATCH_END result=1 reason=0" build/dw2_smoke_a.log || { echo "smoke test FAILED: expected loadout (A) to win on hull, see build/dw2_smoke_a.log"; cat build/dw2_smoke_a.log; exit 1; }
grep -q "^MATCH_END result=0 reason=0" build/dw2_smoke_b.log || { echo "smoke test FAILED: expected empty grid (B) to lose on hull, see build/dw2_smoke_b.log"; cat build/dw2_smoke_b.log; exit 1; }
echo "dw2_server smoke test: A (real loadout) beat B (empty grid) on hull, as hand-derived"

echo "== D2: round-break mini-game -- comeback smoke test (ASan+UBSan) =="
# Proves the actual claim of EMILY/BACKLOG.md SECTION 548 end to end over the real wire protocol,
# not just in the pure-logic unit tests above: two IDENTICAL loadouts (same as the smoke test
# above) fire in perfect lockstep and, left alone, hand-derive to an EXACT TIE at tick 24 (both
# hulls hit 0 on the same tick -- 20 armor each absorbs the tick-3/tick-6 shots, then 15 dmg/hit on
# ticks 9,12,15,18,21,24 drains 100 -> 0 for both, simultaneously). Scripting ONLY ClientA to call
# Overcharge at round-break #2 (after tick 10, hull 75/75 -- still exactly even, so this is the
# baseline non-behind 1.5x payoff, not the comeback-amplified one; that exact value is pinned by
# test_round_overcharge_payoffs in tests/test_core_loop.c instead, no networking needed for it) and
# sleeping for the server-given target_ms turns that tie into a real win for A, three ticks early:
#   round3 (ticks 11-15, A's dmg_mult=1.5x): tick12 A deals 22.5 (B 75->52.5), B deals 15 (A 75->60)
#                                            tick15 A deals 22.5 (B 52.5->30), B deals 15 (A 60->45)
#   round4 (ticks 16-20, back to neutral):   tick18 A deals 15 (B 30->15),   B deals 15 (A 45->30)
#   round5 (ticks 21-25, neutral):           tick21 A deals 15 (B 15->0, DEAD), B deals 15 (A 30->15)
# -> A wins at tick 21 (hull 15% vs 0%), where the untouched baseline would still be an even tie.
DW2_TEST_PORT3=17802
./build/dw2_server --port "$DW2_TEST_PORT3" --no-auth --fast-forward --pack-ms 3000 --tick-ms 20 --verbose \
    > build/dw2_server_round_smoketest.log 2>&1 &
DW2_SRV3_PID=$!
trap 'kill "$DW2_SRV3_PID" 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do ss -ltn 2>/dev/null | grep -q ":$DW2_TEST_PORT3 " && break; sleep 0.1; done

./build/dw2_test_client --port "$DW2_TEST_PORT3" --name RoundA --timeout-ms 20000 \
    --place 0,0,0,0 --place 1,0,1,0 --place 3,0,2,0 --place 4,3,3,0 --round-call 2:overcharge > build/dw2_round_a.log &
ROUND_A_PID=$!
./build/dw2_test_client --port "$DW2_TEST_PORT3" --name RoundB --timeout-ms 20000 \
    --place 0,0,0,0 --place 1,0,1,0 --place 3,0,2,0 --place 4,3,3,0 > build/dw2_round_b.log &
ROUND_B_PID=$!
wait "$ROUND_A_PID"; wait "$ROUND_B_PID"
kill "$DW2_SRV3_PID" 2>/dev/null || true; trap - EXIT

grep -q "^ROUND_CALL round=2 call=overcharge" build/dw2_round_a.log || { echo "round smoke test FAILED: A never sent its scripted round-2 Overcharge call, see build/dw2_round_a.log"; cat build/dw2_round_a.log; exit 1; }
grep -q "^ROUND_RESULT your_call=0 your_grade=3" build/dw2_round_a.log || { echo "round smoke test FAILED: A's round-2 Overcharge call wasn't graded PERFECT (timing landed outside the window), see build/dw2_round_a.log"; cat build/dw2_round_a.log; exit 1; }
grep -q "^MATCH_END result=1 reason=0 ticks=21" build/dw2_round_a.log || { echo "round smoke test FAILED: expected A to win on hull at tick 21 (see comment above for the hand-derived math), see build/dw2_round_a.log"; cat build/dw2_round_a.log; exit 1; }
grep -q "^MATCH_END result=0 reason=0 ticks=21" build/dw2_round_b.log || { echo "round smoke test FAILED: expected B to lose on hull at tick 21, see build/dw2_round_b.log"; cat build/dw2_round_b.log; exit 1; }
echo "round-break smoke test: A's single well-timed Overcharge call turned an identical-loadout tie into a win at tick 21, as hand-derived"

echo "== D2: dw2_client (real interactive client) + headless wire-protocol smoke test (ASan+UBSan) =="
gcc $CFLAGS_BASE -g -fsanitize=address,undefined -fno-sanitize-recover=all $(pkg-config --cflags sdl2) \
    core/items.c core/combat.c core/round.c core/protocol.c core/http.c core/iduna.c apps/client/main.c \
    $(pkg-config --libs sdl2) -o build/dw2_client

DW2_TEST_PORT2=17801
./build/dw2_server --port "$DW2_TEST_PORT2" --no-auth --fast-forward --pack-ms 3000 --tick-ms 20 --verbose \
    > build/dw2_server_d3_smoketest.log 2>&1 &
DW2_SRV2_PID=$!
trap 'kill "$DW2_SRV2_PID" 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do ss -ltn 2>/dev/null | grep -q ":$DW2_TEST_PORT2 " && break; sleep 0.1; done

# --selftest drives the exact same try_place/try_cut/send_ready code paths interactive play uses,
# just auto-scripted, through the real SDL render path (SDL_VIDEODRIVER=dummy, set internally by
# the binary itself -- same convention as apps/local/main.c's own --selftest). Same A-beats-empty-
# grid shape as the D2 smoke test above, but this time proving the real client binary itself (not
# tools/dw2_test_client.c) can complete a full match end to end. --selftest never presses O/B during
# a round-break (see apps/client/main.c's own comment), same neutral no-op default as the smoke
# test above, so the 21-tick result is unaffected; --timeout-ms bumped for the same real added
# round-break wall-clock time (DW2_ROUND_BUDGET_MS x 4 breaks before tick 21).
./build/dw2_client --port "$DW2_TEST_PORT2" --name ClientA --selftest --timeout-ms 20000 > build/dw2_client_a.log &
CLIENT2_A_PID=$!
./build/dw2_client --port "$DW2_TEST_PORT2" --name ClientB --selftest --empty-grid --timeout-ms 20000 > build/dw2_client_b.log &
CLIENT2_B_PID=$!
wait "$CLIENT2_A_PID"; wait "$CLIENT2_B_PID"
kill "$DW2_SRV2_PID" 2>/dev/null || true; trap - EXIT

grep -q "^MATCH_END result=1 reason=0" build/dw2_client_a.log || { echo "D2 client smoke test FAILED: expected dw2_client A (real loadout) to win on hull, see build/dw2_client_a.log"; cat build/dw2_client_a.log; exit 1; }
grep -q "^MATCH_END result=0 reason=0" build/dw2_client_b.log || { echo "D2 client smoke test FAILED: expected dw2_client B (empty grid) to lose on hull, see build/dw2_client_b.log"; cat build/dw2_client_b.log; exit 1; }
echo "dw2_client smoke test: a real interactive-client-shaped binary completed a full match over the actual wire protocol"

echo "BUILD CLEAN"
