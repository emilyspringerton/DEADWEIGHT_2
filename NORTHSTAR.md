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

## D2: server-authoritative 1v1 — done, live-verified (EMILY/BACKLOG.md SECTION 546)

Built for real, not just scoped, with one real correction to this doc's own original plan found
during implementation: **there is no reusable matchmaker binary.** `DEADWEIGHT/apps/matchmaker`
doesn't exist — DEADWEIGHT's own `dw_server` does its own in-process FIFO queueing, never split
into a separate binary. The one real "generic, already-parameterized matchmaker" in the monorepo
(`ECOWAR`/`REDGARDEN`'s `apps/matchmaker`) speaks a UDP wire protocol and forks a new server
process per match — incompatible with DW2's TCP, single-process, real-time-tick design. So
`dw2_server` follows DEADWEIGHT's own actual precedent instead: it queues and pairs connections
itself, no separate matchmaker.

**What's real:**
- `core/protocol.h`/`.c` — a new v1 wire codec (little-endian, `u16 len + u8 type + payload`,
  same framing convention as DEADWEIGHT's own `docs/WIRE_PROTOCOL.md`) for a genuinely different
  match lifecycle than DW's round-lock model: `HELLO`/`AUTH` → `QUEUE` → `MATCH_FOUND` (a timed
  packing phase: `PLACE`/`PANIC_CUT`/`READY`) → `COMBAT_START` → one `TICK` broadcast per
  `--tick-ms` (server-clock-driven `dw2_ship_tick`, not a per-tick player lock — `PANIC_CUT`
  stays legal live mid-combat, per `core/combat.h`'s own doc comment) → `MATCH_END`.
- `apps/server/main.c` (`dw2_server`) — single `poll()` loop, non-blocking TCP, the same
  IDUNA-worker-thread pattern as `dw_server` (verify/report never block the loop). FIFO
  human/bot pairing (bots wait for D4). A match auto-force-starts combat at `--pack-ms` if either
  side hasn't sent `READY` (auto-locking whatever's placed, including an empty grid) — combat
  never hangs on a player who never readies up. Disconnect mid-packing or mid-combat is a real
  forfeit (opponent wins), not a silent hang.
- `core/net.h`/`http.h`/`.c`/`iduna.h`/`.c` — ported from DEADWEIGHT's own infra, `dw2_`-prefixed,
  trimmed to D2's real needs (agent login/verify/report, guest register/login — no friends/duels/
  steam/draft-run/redeem, since this game has none of those systems).
- IDUNA: `internal/games.Registry["deadweight_2"]` (`deadweight_2.play` +
  `deadweight_2.match.write`), migration `202609250900_deadweight2_agents_and_permissions.sql`,
  and a new `DEADWEIGHT2-SERVER` M2M agent (`config/agents.json`) — its own identity, never
  reusing DEADWEIGHT's or ECOWAR's (`ECOWAR-BOTS`'s own precedent). No `game_guest_credentials`/
  `game_player_stats`/`game_matches` migration needed — those tables are already generic
  (`game` column), confirmed live via `internal/games.Registry`'s own doc comment. **No
  `deadweight_2.bot.play` / `DEADWEIGHT2-BOTS` agent yet** — deliberately narrow, matching
  `big_o`'s own precedent: minting a bot identity before D4 has a bot to hold it would be
  speculative scope, not this phase's actual job.

  **Renamed, 2026-09-25** (founder real-time: "just call it D2, disambiguate it from DEADWEIGHT,
  D2 is the official studio name"): every identifier named in this paragraph is historical —
  accurate to what actually shipped in the commit this section describes, left as-is rather than
  rewritten (never edit an applied migration; the same discipline applies to not rewriting this
  doc's own history out from under it). The live, current names are `internal/games.Registry["d2"]`
  (`d2.play`/`d2.match.write`) and the `D2-SERVER` agent, via a new, additive migration
  (`202609251200_rename_deadweight2_to_d2.sql`) rather than an edit to the original one — see
  `core/iduna.h`'s own doc comment and `apps/server/main.c`'s `--agent-name` default for the
  current, real values. The GitHub repo rename itself (`DEADWEIGHT_2` → `D2`) is separately
  blocked on token permissions as of this note — see `EMILY/BACKLOG.md` SECTION 546.
- `tools/dw2_test_client.c` + `scripts/build.sh`'s own new "D2" section — a real, scripted
  headless client (not a stub) that plays a full match over the actual TCP wire protocol.
  Live-verified, not just compiled clean: a real loadout (Generator+Conductor+Railgun+Bulwark,
  the same one `apps/local`'s own `--selftest` fights) beats an empty grid in exactly 21 ticks
  (hand-derivable: 10 energy/tick against a 30 charge threshold fires every 3rd tick × 15 dmg,
  20 armor absorbs the first hit), `MATCH_END` symmetric on both sockets (winner=1/loser=0, same
  reason/ticks). Also verified directly against the raw wire bytes (not just the C client): an
  illegal overlapping placement rejects clean, a `PANIC_CUT` on an unsplittable item (Railgun)
  rejects clean, a match where neither side ever sends `READY` still force-starts combat at the
  pack deadline, and a mid-packing disconnect resolves as a real forfeit for the survivor.

**Real, named simplifications (not oversights):** no rating/Elo update wired into the
`match-result` report body yet (IDUNA's own `game_matches`/`game_player_stats` tables already
compute this generically from `winner`, same as DEADWEIGHT — nothing D2-specific to add here);
guest-only auth path exercised (`--no-auth` mode) — a live IDUNA integration test (real JWT
through `dwi2_verify`) wasn't run in this sandbox, since it needs a running IDUNA instance with
the new migration applied; that's real, deployment-time verification, not a code gap.

**Real client (`dw2_client`), folded into this same D2 phase** (founder real-time: "just call it
D2"; originally scoped as a separate D3 in this doc, folded back in rather than treated as its
own phase): `apps/client/main.c`, a real interactive SDL2 client speaking `core/protocol.h`'s
actual wire protocol end to end — connect, auto-queue, pack a grid against a live opponent within
the server's own time limit (cursor-based placement/rotate/cut, same controls as `apps/local`),
then watch real-time combat driven by the server's own tick clock, Panic Cut still legal mid-
combat. Optional IDUNA guest auth (`--iduna-url`, `--guest-file` persists the guest identity
across relaunches) or a raw `--token`. Real, named simplification (not an oversight, forced by the
protocol itself): `COMBAT_START`/`TICK` only ever carry hull/armor/cargo/shatter **scalars** for
each side, never grid detail — a client literally cannot run `dw2_ship_tick` locally at all (it
needs both ships' full state, and this client never has the opponent's) — so combat renders as
hull/armor HUD bars for both sides, not a live grid; Panic Cut during combat is addressed by
placement index (0-9, printed as a legend at `COMBAT_START`) rather than by clicking a cell.
`--selftest`/`--empty-grid` drive the exact same `try_place`/`try_cut`/`send_ready` code paths as
real keyboard input, headless (`SDL_VIDEODRIVER=dummy`, same convention as `apps/local`'s own
`--selftest`) — wired into `scripts/build.sh` as a second, real interactive-client-shaped smoke
match alongside D2's own `tools/dw2_test_client.c` test (kept, unchanged — it still covers
protocol edge cases this client's smoke test doesn't re-test: illegal placement, cut rejection,
pack-deadline force-start, forfeit). One real bug found and fixed live during this same pass, the
same "verify via the literal bytes/behavior, don't just trust the abstraction" discipline that
caught D2's own `MATCH_FOUND` payload-size bug: the client's connect handshake initially sent an
AUTH message unconditionally whenever a token was in hand, which broke a `--no-auth` server (HELLO
already gets it a `WELCOME`; the now-unexpected trailing AUTH hits `apps/server/main.c`'s own
`DW2_ERR_BAD_STATE` path, closing the connection) — fixed with a short (500ms) probe: a `WELCOME`
within it means no-auth already succeeded and AUTH is never sent; no reply means the server is
silently sitting in `S_NEEDAUTH` (the wire protocol gives no other signal), so AUTH goes out then.
A second bug (an inverted return-value check treating `recv_msg_blocking`'s `-1`-on-timeout as
truthy, so a genuinely auth-required server's silence never triggered the AUTH send at all) was
found live testing the fix against a real `--iduna-url`-configured server and fixed the same pass.

## D2 combat redesign: round-break mini-game, comeback, bluff — SECTION 548 (real V0, built and live-verified)

Founder real-time, 2026-09-25: "like a mini game in between ship auto battler rounds to give a
real time skill check" + "make the games swingy and give comeback mechanics and bluff strategies."
Read as targeting D2 specifically (assumption named, not assumed silently — see the doc below).
Full design write-up (the round-structure decision and why, the mini-game's real rules, the
comeback trigger/effect, the bluff mechanism and what's actually hidden, real V0 vs. deferred
scope) lives in `docs/COMBAT_REDESIGN.md` (golden-doc registered as `DEADWEIGHT2-COMBAT-NORTH`),
not duplicated here. Short version: combat is now itself broken into `DW2_ROUND_TICKS`-tick
rounds (`core/round.h`); between rounds, a real-time "Surge Timing" skill check plus a hidden
Overcharge/Brace call (revealed to both sides only after both are locked in) resolves into a
`dmg_mult` buff, an armor bonus, or (on a missed Overcharge) real self-damage — amplified further
if the calling ship is behind on hull%, the actual comeback lever. Live-verified over the real
wire protocol, not just unit-tested: a new `scripts/build.sh` smoke test scripts one side of an
otherwise-perfectly-symmetric identical-loadout match (which hand-derives to an exact tie at tick
24) to land one well-timed Overcharge call, turning that tie into a real win at tick 21.

## Cannon programming: a real, compiled LO decision function in the live combat loop — SECTION 549 (real V0, built and live-verified)

Founder real-time, 2026-09-25: "add in LO programming to actual gameplay of DEADWEIGHT add
stdlibs to LO whatever is needed that is missing" → "like cannon programming or something."
Read as targeting D2 specifically, same reasoning `docs/LO_CANNON_PROGRAMMING.md` and SECTION 548
above both apply (assumption named, not assumed silently). Full design write-up (the LO capability
audit, the real 4-state input/2-state output design, the build pipeline, why the shipped default
is deliberately the boring "always fire" answer, and what's really deferred) lives in
`docs/LO_CANNON_PROGRAMMING.md`, not duplicated here. Short version: `dw2_ship_tick`'s weapon-fire
check (previously an unconditional "fire when charged") now calls a real, compiled LO program
(`cannon/cannon_decision.llll` → `.prn` → PARENA-generated C, `core/cannon.h`) every tick, packing
the same `behind` signal SECTION 548 already computes plus whether this shot would be lethal into
one real, naturally-4-valued state (LO's own real ceiling is mod-4 arithmetic, checked directly
against `LO/NORTHSTAR.md`, not assumed) — matching the real precedent `LO/NORTHSTAR.md`'s own DUNG
integration section already established for "a genuinely 4-valued decision fits `SWITCH`/`CASE`
today." The shipped decision is FIRE in all 4 states (byte-identical to the pre-existing rule,
verified: every hand-derived smoke-test tick count in this repo is unchanged), with a second, real,
standalone-tested example program (`cannon_bank_on_safe_lead.llll`) proving the mechanism can
express real conditional strategy — not wired live, since holding fire has no compensating
mechanical payoff in this V0 (a real, honestly-discovered failure mode, named in the design doc,
not glossed over). Real, honest finding: LO's own already-named stdlib gaps (matrices, PCRE
matching) turned out to be unrelated to this feature — zero new LO stdlib work was needed here.

## Deferred (not yet built)

Everything `DEADWEIGHT/NORTHSTAR.md` already deferred still applies here: the options-pricing/
insurance/derivatives layer, Merkle-tree cargo-hiding, 2v2/Link Modules, the mobile haptic timing
table, the full 24-item/16-Ultimate catalog, and the tournament bracket. Additionally, this
repo's own remaining phased plan (mirroring `DEADWEIGHT/docs/PHASE_D2..D6`, adapted; the original
D3 "real client shell" line that lived here is now folded into D2 above, not deferred):

- [ ] **Real art + EOSUI Option C chrome** for `dw2_client`, once `EOSUI-NORTH` actually exists
  (`EMILY/docs/EOSUI_NORTHSTAR.md`) — the client today renders immediate-mode colored rectangles
  only, same honest limitation `apps/local`'s own debug shell already named.
- [ ] **D4: a real placeholder bot** (heuristic first, matching `arena_bot_enabled`'s own "no
  local-practice fallback in real matches" convention) so a 1v1 bot pool can exist at all — this
  is when `deadweight_2.bot.play`/`DEADWEIGHT2-BOTS` actually get minted, not before.
- [ ] **D5: PARENA mod integration** for combat-decision edge cases, matching every other
  PARENA-hosted game's "PARENA mod is the trigger, host C does the real work" idiom. Partially real
  now, not fully: the cannon-programming section above wires exactly ONE such decision hook
  (weapon fire, via LO → PARENA) — a real, narrow slice of this idiom, live and tested, not the
  general "any combat decision can be a mod" framework D5 originally named. Widening that to other
  decision points (e.g. Panic Cut timing) is real, separate, not attempted this pass.
