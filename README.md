# DEADWEIGHT_2

The backpack battler from **Dark Sector: Hold Battles** — pack polyomino-shaped cargo items into
a 6x6 grid under a space budget, then fight with the grid you built. Item shape and orientation
route energy from generators to your weapons and defenses; the "waste" left over from cutting an
item down to fit becomes armor. Build a container, then fight with the container you built.

This is a separate repo from the sibling `DEADWEIGHT` (which shipped a different, turn-based
card-mode game first) — see `NORTHSTAR.md` for the full design provenance and why the two are
split.

## Status: Phase D1 + D2 — the core mechanic, a real multiplayer server, and a real client are live

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
- **`dw2_client`** (D2): a real interactive client for `dw2_server` — connects, auto-queues, packs
  a grid against a live opponent (same cursor-based controls as the debug shell) within the
  server's own time limit, then watches real-time combat driven by the server's own tick clock.
  Optional IDUNA guest login (auto-registers and persists the guest identity across relaunches
  with `--guest-file`) or a raw `--token`. Combat renders as hull/armor HUD bars for both sides
  (the wire protocol only ever sends scalars for the opponent, never grid detail — see
  `NORTHSTAR.md`); `tools/dw2_test_client.c` stays the separate, scripted protocol-edge-case test
  tool it always was.
- A headless test suite (`tests/test_core_loop.c`, ASan+UBSan clean) that exercises every rule
  above with exact, hand-derived numbers — not just "it didn't crash." `scripts/build.sh` also
  runs `dw2_server` and `dw2_client` through full real matches over the actual wire protocol
  (ASan+UBSan clean) as part of every build.
- CI (`.github/workflows/ci.yml`) generates a Principle 21 CONSTRUCT snapshot on every run and
  bundles it into each client's own zip, plus standalone `.zip`/`.gz` copies, as build artifacts.

**What's not built yet, honestly:** no real art or shop/HUD chrome for `dw2_client` (colored
rectangles only, same as the debug shell), no real bot (D4), no PARENA integration (D5), no
GitHub Releases/code signing yet (CI produces build artifacts, not tagged releases). See
`NORTHSTAR.md`'s "Deferred" section for the real phased plan.

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
of cursor (the wire protocol never reveals a live grid — see `NORTHSTAR.md`):

```
0-9   Panic Cut the placement at that index (printed as a legend when combat starts)
ESC   quit
```

Feedback (placements, cuts, per-tick hull%) prints to the terminal — the SDL window itself is
deliberately bare-minimum (colored rectangles only) at this phase.

## License

Not yet decided for this repo — see `CLAUDE.md`.
