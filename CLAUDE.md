# DEADWEIGHT_2

## What this is

The backpack battler ("Dark Sector: Hold Battles" VS1 mechanic) from the sibling `DEADWEIGHT`
repo, spun out into its own repo — see `NORTHSTAR.md` for the full why and the real design
provenance (`DEADWEIGHT/NORTHSTAR.md` / `docs/SPEC_REVIEW.md` / `docs/PHASE_D1_CORE_LOOP.md`).
Phase D1 (local-only core loop) and D2 (server-authoritative 1v1, `dw2_server` + the real
interactive client `dw2_client` — folded into D2, not a separate D3, per founder real-time "just
call it D2") are built and live-verified; a real bot and PARENA integration remain named,
not-yet-built phases in `NORTHSTAR.md`'s "Deferred" section.

**Licensing: do not add the Unlicense to this repo.** Explicit founder instruction (2026-09-25),
unlike `SKULDMARK`/`SPIDERBEETLE`'s own convention — licensing is deliberately left unresolved
here. Do not add any `LICENSE` file without a fresh, explicit ask.

## Stack

Hand-written C99, no external dependencies for the core loop or `dw2_server`; SDL2 for the debug
shell (`apps/local/`) and the real client (`apps/client/`). No PARENA yet — a real, named future
phase in `NORTHSTAR.md`'s "Deferred" section.

```bash
./scripts/build.sh           # ASan+UBSan: core-loop tests, SDL debug shell + selftest, dw2_server + dw2_client wire-protocol smoke matches
./build/dw2_local             # play it locally, no networking (needs a display)
./build/dw2_local --selftest  # headless: scripted match through the real render path, no display needed
./build/test_core_loop        # just the headless core-loop test suite
./build/dw2_server --port 7800 --no-auth --fast-forward   # run a local server, no IDUNA needed
./build/dw2_client --port 7800 --name X                   # real interactive client (needs a display)
./build/dw2_client --port 7800 --name X --selftest [--empty-grid]  # headless scripted smoke run, real render path
./build/dw2_test_client --port 7800 --name X --place 0,0,0,0 ...   # scripted headless protocol-edge-case test tool (not a real client)
```

## Layout

- `core/grid.h` — 6x6 triple-bitmask grid + the resolved port-bit/rotation convention.
- `core/items.{h,c}` — the V0 item catalog + authored fragment tables.
- `core/combat.{h,c}` — energy routing, Back-EMF, Panic Cut, win condition.
- `core/dummy.{h,c}` — the fixed hand-authored dummy opponent (not a bot; see `NORTHSTAR.md`).
- `core/protocol.{h,c}` — the D2 wire protocol codec (`docs`-equivalent lives in `NORTHSTAR.md`'s
  own D2 section, not a separate file yet — revisit if the protocol grows past what one doc
  section can hold, matching DEADWEIGHT's own `docs/WIRE_PROTOCOL.md` if it does).
- `core/net.h`/`http.{h,c}`/`iduna.{h,c}` — TCP/HTTP/IDUNA-client infra, ported from DEADWEIGHT's
  own (`dw2_`-prefixed, trimmed to what this game actually calls).
- `apps/local/main.c` — the SDL2 debug shell (pack + fight, immediate-mode boxes only, no
  networking).
- `apps/server/main.c` — `dw2_server`, the D2 authoritative match server (TCP, `poll()`, embedded
  FIFO queue — no separate matchmaker binary; see `NORTHSTAR.md`'s D2 section for why).
- `apps/client/main.c` — `dw2_client`, the D2 real interactive client (connect/queue/pack/fight
  over the actual wire protocol; `--selftest` drives the same real code paths headlessly).
- `tools/dw2_test_client.c` — scripted headless test tool for exercising `dw2_server`'s protocol
  edge cases (reject paths, pack-deadline force-start, forfeit); not a real client.
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

## CONSTRUCT File Generation (standing instruction, monorepo Principle 21)

D2 auto-generates a CONSTRUCT file on every CI run (`.github/workflows/ci.yml`, "Generate Source
Construct" + "Bundle clients + construct"): a deterministic, git-ls-files-based plaintext snapshot
of all tracked source, verified byte-for-byte reproducible on every run (generated twice, `cmp`'d).
Set up like SHANKPIT's own `release.yml` (founder real-time, 2026-09-25): the CONSTRUCT is copied
into each real client's own bundle directory before zipping (`D2_Client_<build>.zip`,
`D2_Local_<build>.zip`), not just uploaded on its own. It also ships standalone, separately, as
both `.zip` and `.gz` (a deliberate, explicit exception to this monorepo's LZ4-by-default
convention, per that same real-time instruction). Every artifact filename carries a `build_<run
number>_<short sha>` tag (SHANKPIT's own convention) so consecutive CI runs never collide under a
generic name. No GitHub Releases/tags yet -- CI build artifacts only (`actions/upload-artifact`);
see `scripts/generate_construct.sh` for local generation (`bash scripts/generate_construct.sh
[OUT.txt]`). No manual edits to the generated file -- it's regenerated from tracked source every
run.

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
