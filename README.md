# DEADWEIGHT_2

The backpack battler from **Dark Sector: Hold Battles** — pack polyomino-shaped cargo items into
a 6x6 grid under a space budget, then fight with the grid you built. Item shape and orientation
route energy from generators to your weapons and defenses; the "waste" left over from cutting an
item down to fit becomes armor. Build a container, then fight with the container you built.

This is a separate repo from the sibling `DEADWEIGHT` (which shipped a different, turn-based
card-mode game first) — see `NORTHSTAR.md` for the full design provenance and why the two are
split.

## Status: Phase D1 only — a local, single-player prototype

**What's real today:**
- The full core mechanic: 6x6 grid, 7 items (Generator/Conductor/Splitter Node for wiring,
  Railgun as the one weapon, Bulwark Plate as the one armor piece, Titanium Beam and Aether Ore
  as splittable cargo), energy routing with real one-to-many (Splitter) and many-to-one-is-inert
  rules, a Back-EMF meltdown that permanently burns out a closed wiring loop, and a live,
  mid-fight Panic Cut that reroutes energy and grants bonus armor from newly exposed Dead
  Squares.
- A playable local debug shell (`apps/local/`): pack a grid by hand, lock it in, and fight a
  fixed dummy loadout in real time.
- A headless test suite (`tests/test_core_loop.c`, ASan+UBSan clean) that exercises every rule
  above with exact, hand-derived numbers — not just "it didn't crash."

**What's not built yet, honestly:** no networking, no accounts, no real bot (the opponent is one
fixed hand-authored grid), no real art or UI (the debug shell draws plain colored rectangles),
no PARENA integration. See `NORTHSTAR.md`'s "Deferred" section for the real phased plan.

## Build & run

Needs a C99 compiler and `libsdl2-dev` (only for the debug shell — the core tests have zero
dependencies beyond libc).

```bash
./scripts/build.sh              # builds + runs the ASan/UBSan test suite, then the debug shell + its selftest
./build/dw2_local                # play it (needs a display)
./build/dw2_local --selftest     # headless: runs one full scripted match through the real render path
./build/test_core_loop           # just the test suite
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

Feedback (placements, cuts, per-tick hull%) prints to the terminal — the SDL window itself is
deliberately bare-minimum (colored rectangles only) at this phase.

## License

Not yet decided for this repo — see `CLAUDE.md`.
