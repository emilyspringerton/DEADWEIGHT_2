# DEADWEIGHT_2

## What this is

The backpack battler ("Dark Sector: Hold Battles" VS1 mechanic) from the sibling `DEADWEIGHT`
repo, spun out into its own repo — see `NORTHSTAR.md` for the full why and the real design
provenance (`DEADWEIGHT/NORTHSTAR.md` / `docs/SPEC_REVIEW.md` / `docs/PHASE_D1_CORE_LOOP.md`).
Phase D1 (local-only core loop) and D2 (server-authoritative 1v1, `dw2_server` + the real
interactive client `dw2_client` — folded into D2, not a separate D3, per founder real-time "just
call it D2") are built and live-verified, including a real-time round-break mini-game (SECTION
548, `docs/COMBAT_REDESIGN.md`) and a real, compiled LO decision function wired into weapon fire
(SECTION 549 "cannon programming," `docs/LO_CANNON_PROGRAMMING.md`); a real bot and the general
"any combat decision can be a PARENA mod" framework remain named, not-yet-built phases in
`NORTHSTAR.md`'s "Deferred" section.

**Licensing: do not add the Unlicense to this repo.** Explicit founder instruction (2026-09-25),
unlike `SKULDMARK`/`SPIDERBEETLE`'s own convention — licensing is deliberately left unresolved
here. Do not add any `LICENSE` file without a fresh, explicit ask.

## Stack

Hand-written C99, no external dependencies for the core loop or `dw2_server`; SDL2 for the debug
shell (`apps/local/`) and the real client (`apps/client/`). One real, narrow PARENA integration
exists (`cannon/`, `core/cannon.{h,c}`, `core/parena_runtime.{h,c}` — see `docs/
LO_CANNON_PROGRAMMING.md`): a weapon-fire decision compiled from LO through PARENA to committed C,
needing no PARENA/LO checkout at ordinary build time (only `scripts/generate_cannon.sh`, when a
`.llll` source changes, does). The general "any combat decision can be a PARENA mod" framework
remains a real, named future phase in `NORTHSTAR.md`'s "Deferred" section.

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
- `core/round.{h,c}` — the round-break mini-game (Surge Timing skill check, Overcharge/Brace
  bluff, comeback amplification); see `docs/COMBAT_REDESIGN.md` (SECTION 548).
- `core/cannon.{h,c}` / `core/parena_runtime.{h,c}` / `cannon/` — "cannon programming": a real,
  compiled LO decision function wired into `dw2_ship_tick`'s weapon-fire check; `cannon/*.llll` is
  the real source, `cannon/*.prn`/`*_gen.c` are committed, generated (do not hand-edit; regenerate
  via `scripts/generate_cannon.sh`); `parena_runtime.{h,c}` is vendored, unmodified, from PARENA.
  See `docs/LO_CANNON_PROGRAMMING.md` (SECTION 549).
- `apps/local/main.c` — the SDL2 debug shell (pack + fight, immediate-mode boxes only, no
  networking).
- `apps/server/main.c` — `dw2_server`, the D2 authoritative match server (TCP, `poll()`, embedded
  FIFO queue — no separate matchmaker binary; see `NORTHSTAR.md`'s D2 section for why).
- `apps/client/main.c` — `dw2_client`, the D2 real interactive client (connect/queue/pack/fight
  over the actual wire protocol; `--selftest` drives the same real code paths headlessly).
- `tools/dw2_test_client.c` — scripted headless test tool for exercising `dw2_server`'s protocol
  edge cases (reject paths, pack-deadline force-start, forfeit); not a real client.
- `tools/dw2_cannon_demo.c` — standalone proof that a second, real LO decision program
  (`cannon/cannon_bank_on_safe_lead.llll`) compiles and behaves as designed; not wired live.
- `tests/test_core_loop.c` — headless ASan+UBSan test suite, the actual "done" bar for D1.
- `tests/test_cannon_hold.c` — standalone `dw2_ship_tick` integration test for the cannon-
  programming `HOLD` branch (needs its own binary — see `docs/LO_CANNON_PROGRAMMING.md`'s own
  "why two binaries" note).

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
generic name. It's also attached as `D2_CONSTRUCT.txt` to every real GitHub Release (see "CI /
auto-release" below). See `scripts/generate_construct.sh` for local generation (`bash
scripts/generate_construct.sh [OUT.txt]`). No manual edits to the generated file -- it's
regenerated from tracked source every run.

## CI / auto-release (EMILY/BACKLOG.md SECTION 551)

Every green push to `main` auto-bumps the MINOR version (`git tag -l 'v[0-9]*.[0-9]*.[0-9]*'
--sort=-v:refname`, same exact logic as DEADWEIGHT's own `version` job — copied verbatim, it's
already been through one real bug fix there, a parallel-push tag race, 2026-09-18), tags it, and
publishes a real GitHub Release (`dw2_server_linux_x86_64`, `dw2_client_linux_x86_64`,
`dw2_local_linux_x86_64`, `D2_CONSTRUCT.txt`) via `gh release create ... --target "$GITHUB_SHA"`
(a plain `git push origin $TAG` is rejected for workflow-permission reasons, same as DEADWEIGHT).
**Windows client added (SECTION 551 follow-up, 2026-09-25)**: a `windows` job cross-compiles
`dw2_client.exe` via mingw (no mbedTLS needed — D2's `http.h` is plain-HTTP-only, unlike
DEADWEIGHT's own Windows build) and attaches `dw2_client_windows.zip` (flat: exe + `SDL2.dll` +
`PLAY.bat`, byte-for-byte the same layout as DEADWEIGHT's own real `dw_gui_windows.zip`) to every
release. See `docs/WINDOWS_CLIENT_BUILD.md`. `dw2_server`/`dw2_local` and any Android build surface
stay out of scope — do not add those without a real, separate scoping pass (see the root
CLAUDE.md's own D2 row).
`DW2_VERSION` (env var, default `0.0.0-dev`) is threaded through `scripts/build.sh` into
`-DDW2_VERSION`, which `apps/server/version.h`/`main.c` reads for `dw2_server --version`.

The same `release` job GPG-signs each of the three binaries and publishes them to IDUNA's
app-release registry (`POST /api/v1/app-releases`, `app_slug="d2"` — `IDUNA/internal/games/
games.go` `Registry["d2"]`), matching DEADWEIGHT's own step (`IDUNA/docs/APP_RELEASE_SIGNING.md`)
almost verbatim, including its clean no-op-and-exit-0 gate on the two secrets
(`EINHORN_APP_RELEASES_GPG_PRIVATE_KEY`, `APP_RELEASES_CI_SECRET`) — neither is provisioned on
this repo yet, same as DEADWEIGHT's own current state, so this step logs a skip message and exits
0 rather than failing an otherwise-green build.

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
