# DEADWEIGHT_2 — the backpack battler, spun out on its own

## What this is and why it's a separate repo

`DEADWEIGHT` (the sibling repo in this monorepo) shipped VS0 — a card-mode 1v1 game — first,
per `DEADWEIGHT/docs/VS0_SCOPING.md`'s own real build-order call: "card mode ships first (small
discrete action space, no inventory UI, bot- and RL-friendly)... the backpack battler... becomes
**VS1**." This repo is that VS1 mechanic, given its own home rather than bolted onto DEADWEIGHT's
existing card-mode server/client/wire-protocol/account plumbing — the two are different enough
games (turn-based card combat vs. a spatial-packing-into-real-time-combat sim) that a separate
repo keeps DEADWEIGHT's own VS0 surface stable while this one grows independently.

Founder real-time, this session: "build DEADWEIGHT_2 (upstream repo created)... find the backpack
battler plans in the founding deadweight docs." Routed via `emily observe` first per
`EMILY/docs/THE_EMILY_WAY.md` Principle 18. Explicit instruction: **do not add the Unlicense** to
this repo (unlike `SKULDMARK`/`SPIDERBEETLE`'s own convention) — licensing is deliberately left
unresolved here, not an oversight; see `CLAUDE.md`.

## Source of the design (full detail lives in the sibling `DEADWEIGHT` repo, not duplicated here)

- `DEADWEIGHT/NORTHSTAR.md` — the original real critical review of the founding transcript
  (`DEADWEIGHT/LeetCode Skills Course Curriculum.pdf`, a captured Gemini chat transcript that
  spirals from a LeetCode teaching curriculum into the full "Dark Sector: Hold Battles" game
  spec), and the V0 item/Ultimate/tournament scope cut this repo inherits.
- `DEADWEIGHT/docs/SPEC_REVIEW.md` — the second-pass holes-and-resolutions review (fragment
  tables authored per item, the win-condition tiebreak, one-to-many-not-many-to-one energy
  routing, the port-bit rotation convention, the "Sniped!" draft-race rule for a later networked
  phase). Every rule this repo's `core/` implements traces to a specific resolution in that doc.
- `DEADWEIGHT/docs/PHASE_D1_CORE_LOOP.md` — the exact phase this repo's first real slice
  implements: **local-only, zero networking/accounts/UI-polish, one human vs. one fixed dummy
  grid**, to prove the mechanic is fun before a server or a second client exists.

## What's actually built here (Phase D1 — done, live-verified)

- `core/grid.h` — the 6x6 triple-bitmask grid (Occupancy/Blockade/Ruined Real Estate), the
  transcript's own `M_Item & (M_O|M_B|M_R) == 0` legality formula adopted as-is, and the resolved
  port-bit convention (bit3=N, bit2=E, bit1=S, bit0=W; a 90°-CW rotation is the cyclic
  `(v>>1)|((v&1)<<3)` rotate).
- `core/items.c` — the V0 catalog: **7 items**, one per SPEC_REVIEW's own worked fragment
  examples plus the minimum wiring/combat piece set:
  - **Generator**, **Conductor**, **Splitter Node** (Operations) — the wiring pieces.
  - **Railgun** (Offense, the one V0 weapon), **Bulwark Plate** (Defense, the one V0 armor piece).
  - **Titanium Beam** (Offense cargo, halvable — SPEC_REVIEW's own "1x4 → two 1x3 (2 base + 1
    dead)" example, verbatim) and **Aether Ore** (Defense cargo, quarterable — "2x2 → four 1x2
    (1 base + 1 dead)", verbatim). Fragment shapes are **authored data, not a runtime geometry
    algorithm**, exactly as SPEC_REVIEW §1 resolved.
- `core/combat.c` — energy routing (generators → conductors/splitters → weapons, one-to-many
  legal via Splitter, many-to-one inert per SPEC_REVIEW §3), Back-EMF closed-loop meltdown
  (`Φ(t)=Φ0·e^{k·t}`-shaped growth, shatters past 2.5·Φ0, permanently burns the loop's cells),
  Panic Cut (live mid-fight, not just at grid-lock — vacated cells become Ruined Real Estate,
  newly exposed Dead Squares grant immediate bonus armor), hull%-primary / hull%-then-cargo-value
  tiebreak win condition (SPEC_REVIEW §2).
- `apps/local/main.c` — the bare-minimum SDL2 debug shell PHASE_D1 itself calls for: pack a grid
  by hand (number keys select an item, arrows move a cursor, R rotates, Enter places, C
  Panic-Cuts), F locks in and fights a fixed dummy loadout in real time. Not the real client UI —
  that's D3 in the original phased plan.
- `tests/test_core_loop.c` — headless (no SDL), ASan+UBSan clean, 53 checks: placement legality,
  rotation math, Panic Cut (including a cut blocked by a neighbor, and cutting an already-cut or
  unsplittable item failing clean), a genuine hand-built 4-cell closed energy loop shattering on
  the exact predicted tick, one-to-many and many-to-one both verified by exact charge counts (not
  just "no crash"), the timeout hull%/cargo-value tiebreak, and a 500-seed random-placement fuzz
  pass across full matches.

All of PHASE_D1_CORE_LOOP.md's own acceptance criteria are met: two differently-packed loadouts
produce different outcomes (verified via the many-to-one/one-to-many/straight-chain tests, each
hand-derived and cross-checked against the actual running binary, not just asserted), a
deliberately-built closed loop shatters (not crashes or silently no-ops), a mid-fight Panic Cut
changes live routing, and the fuzz pass runs clean under ASan+UBSan.

## Real, named simplifications in this V0 slice (not oversights)

- **Back-EMF tracks one combined loop region per ship, not independently-many.** If a player
  builds two simultaneous, physically distinct closed loops in the same tick, they're detected
  and shattered as one merged mask rather than two independent trackers. `core/combat.c`'s own
  doc comment on `dw2_ship_tick` names this; revisit only if real playtesting finds multi-loop
  builds common enough to matter.
- **The dummy opponent (`core/dummy.c`) is a fixed, hand-authored loadout, not a bot.** A real
  bot is explicitly D2's job in the original phased plan, once there's a server to run one
  against — `DEADWEIGHT/NORTHSTAR.md`'s own named gap ("ECOWAR's bot AI has no idea what a
  'polyomino' is... a real bot for this game needs its own, new AI").
- **The debug shell has no drag-and-drop, no art, no bitmap font renderer.** Feedback during
  interactive play is printed to stdout (placements, cuts, per-tick hull%); the SDL window
  renders only colored rectangles. This is the explicit PHASE_D1 scope ("bare-minimum debug
  render... not the real client UI").
- **Visual verification was done via the SDL dummy video driver's `--selftest`, not a real
  on-screen screenshot.** `apps/local/main.c --selftest` runs a full scripted match through the
  real render path (`draw_ship`/`SDL_RenderPresent`) headlessly and asserts a real match resolves;
  a genuine on-screen capture in this sandbox hit Xvfb/background-process flakiness and wasn't
  worth fighting for a bare-boxes debug view — a real screenshot pass is easy to add later if a
  visual regression is ever suspected.
- **Cargo value counts a cut item's full original value even after fragmentation** — SPEC_REVIEW
  never specifies whether cutting should devalue an item's cargo-value contribution, and cargo
  value is meta/tiebreak-only (never a primary win condition), so this was left as the simplest
  reading rather than guessed at further.

## Deferred (not this repo's V0, matching `DEADWEIGHT/NORTHSTAR.md`'s own cut)

Everything `DEADWEIGHT/NORTHSTAR.md` already deferred still applies here: the options-pricing/
insurance/derivatives layer, Merkle-tree cargo-hiding, 2v2/Link Modules, the mobile haptic timing
table, the full 24-item/16-Ultimate catalog, and the tournament bracket. Additionally, this
repo's own phased plan (mirroring `DEADWEIGHT/docs/PHASE_D2..D6`, adapted):

- [ ] **D2: server-authoritative 1v1.** A new hand-written C `dw2_server` (same architecture as
  DEADWEIGHT's `apps/server`) + a matchmaker reusing `apps/matchmaker`'s own generic, already-
  parameterized binary. IDUNA: new `game='deadweight_2'` scope, its own M2M agent identity (never
  reusing DEADWEIGHT's or ECOWAR's — `ECOWAR-BOTS`'s own precedent), guest-account auth (the
  `provider="guest"` work DEADWEIGHT's own NORTHSTAR already scoped in detail, reused here rather
  than re-designed).
- [ ] **D3: a real client shell** replacing the debug renderer — drag/drop-or-cursor packing UX,
  real art, EOSUI Option C once it exists (`EMILY/docs/EOSUI_NORTHSTAR.md`) for the shop/HUD
  chrome.
- [ ] **D4: a real placeholder bot** (heuristic first, matching `arena_bot_enabled`'s own "no
  local-practice fallback in real matches" convention) so a 1v1 bot pool can exist at all.
- [ ] **D5: PARENA mod integration** for combat-decision edge cases, matching every other
  PARENA-hosted game's "PARENA mod is the trigger, host C does the real work" idiom — not
  attempted in D1, since D1 deliberately has zero external dependencies beyond SDL2.
