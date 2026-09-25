# DEADWEIGHT_2

## What this is

The backpack battler ("Dark Sector: Hold Battles" VS1 mechanic) from the sibling `DEADWEIGHT`
repo, spun out into its own repo — see `NORTHSTAR.md` for the full why and the real design
provenance (`DEADWEIGHT/NORTHSTAR.md` / `docs/SPEC_REVIEW.md` / `docs/PHASE_D1_CORE_LOOP.md`).
Phase D1 (local-only core loop, no networking/accounts) is built and live-verified; everything
past that is a named, not-yet-built phase in `NORTHSTAR.md`.

**Licensing: do not add the Unlicense to this repo.** Explicit founder instruction (2026-09-25),
unlike `SKULDMARK`/`SPIDERBEETLE`'s own convention — licensing is deliberately left unresolved
here. Do not add any `LICENSE` file without a fresh, explicit ask.

## Stack

Hand-written C99, no external dependencies for the core loop; SDL2 only for the debug shell
(`apps/local/`). No PARENA, no networking, no IDUNA integration yet — all real, named future
phases in `NORTHSTAR.md`, not overlooked.

```bash
./scripts/build.sh          # ASan+UBSan core-loop tests, then the SDL debug shell + its selftest
./build/dw2_local           # play it (needs a display)
./build/dw2_local --selftest  # headless: scripted match through the real render path, no display needed
./build/test_core_loop      # just the headless test suite
```

## Layout

- `core/grid.h` — 6x6 triple-bitmask grid + the resolved port-bit/rotation convention.
- `core/items.{h,c}` — the V0 item catalog + authored fragment tables.
- `core/combat.{h,c}` — energy routing, Back-EMF, Panic Cut, win condition.
- `core/dummy.{h,c}` — the fixed hand-authored dummy opponent (not a bot; see `NORTHSTAR.md`).
- `apps/local/main.c` — the SDL2 debug shell (pack + fight, immediate-mode boxes only).
- `tests/test_core_loop.c` — headless ASan+UBSan test suite, the actual "done" bar for D1.

## Founder Real-Time Direction

Route any founder real-time direction — a new ask, a correction, a "can we also..." — through
`emily observe -s info "Founder real-time: <summary>"` first, then log it into
`EMILY/BACKLOG.md` as a real, scoped section, and only then implement. See
`EMILY/docs/THE_EMILY_WAY.md` Principle 18 ("Pave the Cow Paths").

## Apple Filing Protocol

```bash
emily apples post -t completion -repo DEADWEIGHT_2 "<title>" "<body with commit hash>"
```
Then mark the item done in `EMILY/BACKLOG.md` and commit.

## CHANGELOG Protocol

```bash
emily changelog add DEADWEIGHT_2 "<what changed>"
# or manually: append a dated bullet under ## YYYY-MM-DD in CHANGELOG.md
```

## Golden Doc Registration

Any new NORTHSTAR.md/architecture doc here must be added to
`EMILY/context/golden-docs-index.md` so Emily Prime picks it up:
```bash
cd /home/fatbaby/EMILY && git add context/golden-docs-index.md && git commit -m "golden-index: add DEADWEIGHT2-NORTH" && git push
```

## README Reality — SAGA reconciliation (standing instruction, monorepo-wide)

If a change substantially changes the claim of this repo's own README (a capability added/
removed, status moves from "design only" to "working," build/run steps change), update
`README.md` in the same unit of work. See the root `/home/fatbaby/CLAUDE.md`'s own "README
Reality" section for the full rationale.

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — the default for every repo in this monorepo.
Every commit carries a `session: <tag>` trailer (`emily session current`), blank line before it.
