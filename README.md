# DEADWEIGHT_2

The backpack battler from **Dark Sector: Hold Battles** — pack polyomino-shaped cargo items into
a 6x6 grid under a space budget, then fight with the grid you built. Item shape and orientation
route energy from generators to your weapons and defenses; the "waste" left over from cutting an
item down to fit becomes armor. Build a container, then fight with the container you built.

This is a separate repo from the sibling `DEADWEIGHT` (which shipped a different, turn-based
card-mode game first) — see `NORTHSTAR.md` for the full design provenance and why the two are
split.

## Status: Phase D1 + D2 — the core mechanic, a real multiplayer server, a real client, a round-break mini-game, and cannon programming are live

**What's real today:**
- The full core mechanic: 6x6 grid, 7 items (Generator/Conductor/Splitter Node for wiring,
  Railgun as the one weapon, Bulwark Plate as the one armor piece, Titanium Beam and Aether Ore
  as splittable cargo), energy routing with real one-to-many (Splitter) and many-to-one-is-inert
  rules, a Back-EMF meltdown that permanently burns out a closed wiring loop, and a live,
  mid-fight Panic Cut that reroutes energy and grants bonus armor from newly exposed Dead
  Squares.
- A playable local debug shell (`apps/local/`): pack a grid by hand, lock it in, and fight a
  fixed dummy loadout in real time.
- **`dw2_server`** (D2): a real, server-authoritative TCP match server — queue, get paired
  1v1, pack your grid against a live opponent within a time limit, then fight it out over a
  real-time tick loop the server alone drives (a live `PANIC_CUT` stays legal mid-fight). Optional
  IDUNA guest-account auth + match-result reporting (`game='d2'`); runs `--no-auth` for
  local testing with no IDUNA instance needed.
- **Round-break mini-game** (EMILY/BACKLOG.md SECTION 548): combat is now broken into 5-tick
  rounds; between rounds, a real-time "Surge Timing" skill check (hit the server's own stated
  target, server-measured — no client-side timing to fake) plus a hidden Overcharge/Brace call
  (revealed to both sides only after both are locked in — a real bluff layer) resolves into a
  damage buff, an armor bonus, or — on a missed Overcharge — real self-damage, amplified further
  if the calling ship is behind on hull% (the comeback lever). Full design + the exact payoff
  table in `docs/COMBAT_REDESIGN.md`. Live-verified over the real wire protocol: a dedicated
  `scripts/build.sh` smoke test shows one well-timed Overcharge call turning an otherwise-exact
  tie (two identical loadouts) into a real win, three ticks early.
- **`dw2_client`** (D2): a real interactive client for `dw2_server` — connects, auto-queues, packs
  a grid against a live opponent (same cursor-based controls as the debug shell) within the
  server's own time limit, then watches real-time combat driven by the server's own tick clock.
  Optional IDUNA guest login (auto-registers and persists the guest identity across relaunches
  with `--guest-file`) or a raw `--token`. Combat renders as hull/armor HUD bars for both sides
  (the wire protocol only ever sends scalars for the opponent, never grid detail — see
  `NORTHSTAR.md`); a round-break prints the skill-check target to the console and accepts an O
  (Overcharge) / B (Brace) keypress. `tools/dw2_test_client.c` stays the separate, scripted
  protocol-edge-case test tool it always was, now also scriptable for the round-break mini-game
  (`--round-call ROUND:overcharge|brace`).
- **Cannon programming** (EMILY/BACKLOG.md SECTION 549): the weapon-fire decision in
  `dw2_ship_tick` is a real, compiled LO program (`cannon/cannon_decision.llll`, via `parena
  build`'s own real C backend), not a hardcoded rule — packs `behind` (the same signal the
  round-break comeback lever uses) and "would this shot be lethal" into one real, naturally-4-
  valued state, matching LO's own real mod-4 ceiling. The shipped decision is FIRE in all 4 states
  (byte-identical to the rule it replaces — every hand-derived match number in this README is
  unchanged); a second, real, standalone-tested example program shows the same mechanism holding
  fire on a safe, non-lethal lead. No sibling LO/PARENA checkout is needed to build or run D2 —
  the generated C is committed; `scripts/generate_cannon.sh` regenerates it if a `.llll` source
  changes. Full design in `docs/LO_CANNON_PROGRAMMING.md`.
- A headless test suite (`tests/test_core_loop.c`, ASan+UBSan clean, 100 checks) that exercises
  every rule above — including the round-break grading/payoff table, with zero networking needed
  — with exact, hand-derived numbers, not just "it didn't crash." `scripts/build.sh` also runs
  `dw2_server` and `dw2_client` through full real matches over the actual wire protocol (ASan+UBSan
  clean) as part of every build.
- CI (`.github/workflows/ci.yml`) generates a Principle 21 CONSTRUCT snapshot on every run and
  bundles it into each client's own zip, plus standalone `.zip`/`.gz` copies, as build artifacts.
  Every green push to `main` also auto-bumps the minor version, tags it, and publishes a real
  GitHub Release (`dw2_server`/`dw2_client`/`dw2_local` + the CONSTRUCT) — see "CI / releases"
  below.

**What's not built yet, honestly:** no real art or shop/HUD chrome for `dw2_client` (colored
rectangles only, same as the debug shell; the round-break's own target/timer is console-printed
only, no on-screen countdown), no real playtesting/balance pass on the round-break's timing
windows or payoff numbers, no round-break during `apps/local`'s fixed dummy fight (bluffing needs
a live, reactive opponent), no real bot (D4, so no bot decision logic for the round-break call or
cannon programming either), no charge-banking payoff for a cannon program that holds fire (so the
example "hold on a safe lead" program isn't wired live — see `docs/LO_CANNON_PROGRAMMING.md`), no
general "any combat decision can be a PARENA mod" framework (D5 — cannon programming is one real,
narrow slice of that idiom, not the whole thing). GPG-signed IDUNA app-release publishing is wired
into CI (same mechanism DEADWEIGHT uses, `app_slug="d2"`) but degrades to a clean no-op until its
two GitHub Actions secrets are actually provisioned on this repo — see "CI / releases" below and
`IDUNA/docs/APP_RELEASE_SIGNING.md`. See `NORTHSTAR.md`'s "Deferred" section,
`docs/COMBAT_REDESIGN.md`'s own "Deferred" section, and `docs/LO_CANNON_PROGRAMMING.md`'s own
"Deferred" section for the real phased plan.

## Build & run

Needs a C99 compiler and `libsdl2-dev` (only for the debug shell and client — the core tests have
zero dependencies beyond libc).

```bash
./scripts/build.sh              # builds + runs the ASan/UBSan test suite, the debug shell + selftest, dw2_server + dw2_client wire-protocol matches
./build/dw2_local                # play it locally, no networking (needs a display)
./build/dw2_local --selftest     # headless: runs one full scripted match through the real render path
./build/test_core_loop           # just the test suite
./build/dw2_server --port 7800 --no-auth --fast-forward   # run the multiplayer server locally, no IDUNA needed
./build/dw2_client --port 7800 --name YourName             # connect and play a real match (needs a display)
```

## Controls (debug shell)

```
1-7          select an item from the catalog
arrows/WASD  move the grid cursor
R            rotate the selected item / the item under the cursor
ENTER        place the selected item at the cursor (packing phase only)
C            Panic Cut the item under the cursor (legal while packing OR mid-fight)
F            lock the grid and start the fight
ESC          quit
```

## Controls (`dw2_client`)

Same packing controls as the debug shell (`1-7`/arrows-WASD/`R`/`ENTER`/`C`), plus `F` to send
READY instead of locking a local fight. During combat, `PANIC_CUT` is by placement index instead
of cursor (the wire protocol never reveals a live grid — see `NORTHSTAR.md`); when a round-break
opens (every 5 ticks), the console prints the real-time skill-check target and `O`/`B` respond:

```
0-9   Panic Cut the placement at that index (printed as a legend when combat starts)
O     Overcharge call during a round-break (high ceiling, backfires on a missed timing check)
B     Brace call during a round-break (safe, low ceiling -- a miss costs nothing)
ESC   quit
```

Feedback (placements, cuts, per-tick hull%) prints to the terminal — the SDL window itself is
deliberately bare-minimum (colored rectangles only) at this phase.

## CI / releases

`.github/workflows/ci.yml`: every push builds/tests everything and bundles the CONSTRUCT; every
green push to `main` auto-bumps the minor version, tags it (`vX.Y.0`), and publishes a real GitHub
Release with `dw2_server_linux_x86_64`, `dw2_client_linux_x86_64`, `dw2_local_linux_x86_64`,
`dw2_client_windows_x86_64.exe` + `SDL2.dll`, and `D2_CONSTRUCT.txt` attached. The Windows client
is cross-compiled via mingw (structurally verified — real `PE32+` binary — but not yet
runtime-tested on actual Windows; see `docs/WINDOWS_CLIENT_BUILD.md`); the server and local debug
shell stay Linux-only for now (Android build surface still doesn't exist, unlike DEADWEIGHT). The
same job GPG-signs each Linux binary and publishes it to IDUNA's app-release registry
(`app_slug="d2"`, `IDUNA/docs/APP_RELEASE_SIGNING.md`) — this step no-ops cleanly until its two
GitHub Actions secrets are provisioned on this repo, same current state as DEADWEIGHT's own copy
of the same step; the Windows client isn't in that signing loop yet (a cheap, named follow-on, not
done in this pass).

## License

Not yet decided for this repo — see `CLAUDE.md`.
