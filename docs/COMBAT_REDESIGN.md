# D2 Combat Redesign — round-break mini-game, comeback, bluff

`EMILY/BACKLOG.md` SECTION 548. Founder real-time, 2026-09-25, across two rapid messages: "like a
mini game in between ship auto battler rounds to give a real time skill check" and "make the games
swingy and give comeback mechanics and bluff strategies." Routed via `emily observe` (obs
`2026-09-25T10-46-42Z`, Apple #20808) per `EMILY/docs/THE_EMILY_WAY.md` Principle 18, then logged
into BACKLOG SECTION 548, then worked — Backlog First / Apple Before Mark-Done order.

## Assumption named, not assumed silently

"Ship auto battler" is read as targeting **D2** specifically — D2 is the sibling repo with ships
built from a packed grid; the sibling `DEADWEIGHT` proper is a turn-based card game with no ships
at all. This is BACKLOG's own inference, not a founder confirmation. If that reading is wrong,
everything below still stands as a real, working feature — it would just need re-scoping onto
DEADWEIGHT's own card-mode round structure instead, a different (if related) design problem.

## The real tension this doc resolves

D2 combat today (`core/combat.c`'s `dw2_ship_tick`, driven by `apps/server/main.c`'s `run_tick`)
is one continuous, server-clock-driven tick loop from `COMBAT_START` to `MATCH_END` — there is no
concept of a "round" anywhere in the sim. "A mini-game in between rounds" is not implementable as
a bolt-on; it requires deciding what a round actually becomes in a game that never had one, with
real consequences for `core/protocol.h`'s wire format and `apps/server/main.c`'s tick loop.

**Decision: combat is restructured into fixed `DW2_ROUND_TICKS`-tick bursts ("rounds").** After
each burst — except the very last tick before the match timeout, since there's no point opening a
mini-game combat is about to end regardless of its outcome — a **round-break** pauses ticking for
the mini-game, then combat resumes. `DW2_ROUND_TICKS = 5` (`core/round.h`), so a full 30-tick match
(`DW2_MATCH_TIMEOUT_TICKS`) has up to 5 rounds and 4-5 breaks, depending on how early the match
ends.

Alternatives considered and rejected:
- **Map "rounds" onto Back-EMF loop cycles.** Rejected: loop cycles are a per-ship, optional,
  player-triggered event (many matches have zero), not a shared beat both players experience at
  the same time — a mini-game needs a moment both sides are actually IN together.
- **Map "rounds" onto fixed real-time wall-clock intervals** (e.g. every 5 real seconds) instead of
  fixed tick counts. Rejected as a distinction without a difference here: today 1 tick ≈ 1 real
  second under normal `--tick-ms`, so a tick-count boundary and a wall-clock boundary coincide
  anyway; ticks are the unit the rest of the sim (`elapsed_ticks`, `DW2_MATCH_TIMEOUT_TICKS`) is
  already expressed in, so anchoring rounds to ticks keeps one source of truth instead of two.

## The mini-game: "Surge Timing" (a real, active skill check)

At each round-break the server (`apps/server/main.c`'s `begin_round_break`) computes a **target
press time** (`dw2_round_target_ms`, `core/round.h`): a value in `[DW2_ROUND_TARGET_MIN_MS,
DW2_ROUND_TARGET_MIN_MS + DW2_ROUND_TARGET_MS_SPAN)` = `[300, 900]` ms, deterministically derived
from `(match_seed, round_no)` via a splitmix32-shaped hash — both sides are told the exact same
target, and a match's mini-game outcomes are fully reproducible given its seed and the calls made
(same discipline `dw2_test_client.c`'s own seeded matches already lean on).

`DW2_S_ROUND_BREAK` tells the player this `target_ms` outright (see `core/protocol.h`'s own doc
comment) plus the total real-time `budget_ms` (`DW2_ROUND_BUDGET_MS = 1200`). The player must send
`DW2_C_ROUND_CALL` as close to `target_ms` **after receiving** `ROUND_BREAK` as they can — a real
reaction-timing action (closer to a rhythm-game "hit the beat" than a dice roll), not a passive
stat check. Grading is **server-authoritative**: the server times the real wall-clock gap between
sending `ROUND_BREAK` and receiving `ROUND_CALL` (`resolve_round_break`) — it never trusts a
client-reported timestamp, so there's no timing-cheat vector.

```
diff = |elapsed_ms - target_ms|
diff <= DW2_ROUND_PERFECT_MS (80)   -> PERFECT
diff <= DW2_ROUND_GOOD_MS    (220)  -> GOOD
otherwise, or no call by the deadline -> MISS (or NONE if no call arrived at all)
```

A player who never sends `ROUND_CALL` at all is graded `DW2_GRADE_NONE`, always with a fully
neutral effect — you can't be punished (or rewarded) for a mini-game you never engaged with. This
is what keeps the pre-redesign smoke tests (`scripts/build.sh`'s original A-beats-empty-grid test)
byte-identical: neither scripted client sends a call, so every round-break is a no-op and the
original hand-derived 21-tick result is unchanged.

## The bluff: hidden Overcharge/Brace call

Alongside timing, each `ROUND_CALL` carries a **call**: `DW2_CALL_OVERCHARGE` (0) or
`DW2_CALL_BRACE` (1) — a real, hidden decision, not just a timing number. Both sides' calls are
locked in privately and revealed to each other **only** in the following `DW2_S_ROUND_RESULT`,
simultaneously, after both are already committed (or the deadline has passed). A player can read
or bait an opponent's pattern across rounds — always Bracing, then Overcharging exactly when it
counts, or the reverse as a bluff — the same real information asymmetry a hidden-hand card game
gets from concealed cards, here built from an active per-round choice instead of a static deal.

This sits alongside `core/protocol.h`'s own already-existing hidden information (`COMBAT_START`/
`TICK` never carry grid/placement detail, per that header's own doc comment) — the round-break
adds a second, *active*, per-round layer of hidden information on top of that *static* one.

Payoff table (`dw2_round_grade`, `core/round.c`) — deliberately asymmetric risk profiles, so the
choice is a real bet, not a strictly-dominant option:

| Call | PERFECT | GOOD | MISS |
|---|---|---|---|
| **Overcharge** | dmg_mult ×1.5 (×2.0 if behind) for the next round | dmg_mult ×1.25 (×1.5 if behind) | **backfires**: 8% self hull damage, immediately |
| **Brace** | +25 armor (+35 if behind), immediate, permanent | +12 armor (+18 if behind) | nothing — no bonus, no penalty |

Overcharge is the high-ceiling, real-risk call (a missed timing check hurts you); Brace is the
safe, low-ceiling call (a miss just does nothing). A player broadcasting confident Overcharge calls
could be genuinely ahead on execution, or bluffing a read they don't actually have — the downside
if they whiff is real, so it isn't a free tell either way.

## The comeback: behind amplifies a successful call

`behind` (`core/protocol.h`'s own `round_break.behind` field) is computed fresh at the moment of
grading: `self.hull_pct < opponent.hull_pct`, strictly. A ship that is behind and lands a graded
call (PERFECT or GOOD, either call type) gets the amplified payoff column in the table above — the
losing side's own skill-check success matters mechanically more, not just "try harder." This is
grounded directly in state D2 already tracks and computes every tick (`hull_pct`) — no new stat was
invented to hang this off of.

This is a REAL mechanical lever, not a cosmetic one: `test_round_overcharge_payoffs`
(`tests/test_core_loop.c`) pins the exact numbers — a BEHIND ship's GOOD grade (×1.5) matches an
AHEAD ship's own PERFECT grade (×1.5) exactly, so a losing player under real time pressure doesn't
need frame-perfect timing to swing the fight back, just a decent one.

## V0 built and live-verified (not just scoped)

Everything above is real, shipped code, not a plan:

- `core/round.h`/`round.c` — pure game logic (`dw2_round_target_ms`, `dw2_round_grade`,
  `dw2_ship_apply_round_effect`), zero networking/SDL dependency, unit-tested directly
  (`tests/test_core_loop.c`: `test_round_target_and_grading_windows`,
  `test_round_overcharge_payoffs`, `test_round_brace_payoffs`, `test_round_no_call_is_neutral`,
  `test_round_apply_effect`, `test_dmg_mult_scales_damage` — 43 new checks, 96 total, 0 failures).
- `core/combat.h`/`combat.c` — a new `Dw2Ship.dmg_mult` field (default 1.0, reset at
  `dw2_ship_start_combat`), wired into `dw2_ship_tick`'s own damage application
  (`DW2_WEAPON_DAMAGE * self->dmg_mult`) — the ONLY change to the existing D1 combat sim; every
  other D1 rule (grid, items, energy routing, Back-EMF, Panic Cut) is untouched.
- `core/protocol.h`/`protocol.c` — 3 new, additive wire messages: `DW2_S_ROUND_BREAK`,
  `DW2_C_ROUND_CALL`, `DW2_S_ROUND_RESULT`. `DW2_PROTO_VERSION` stays 1 (both new payloads are
  fixed-size); no separately-deployed old binaries exist yet to worry about compatibility with
  (README.md: CI build artifacts only, no tagged releases).
- `apps/server/main.c` — `begin_round_break`/`resolve_round_break`/`handle_round_call`, plus
  `Match`'s own new round-tracking fields; `run_tick` now opens a break every `DW2_ROUND_TICKS`
  ticks (unless the match timeout is already reached); `expire_timers` resolves a break on its real
  wall-clock deadline if not both sides have called first.
- `tools/dw2_test_client.c` — extended (not replaced) with `--round-call ROUND:overcharge|brace`,
  used to script a real, timed response in a smoke test; the DEFAULT (no flag) behavior is
  unchanged — stays silent through every round-break, exactly like before this redesign existed.
- `apps/client/main.c` — the real interactive client prints `ROUND_BREAK`/`ROUND_RESULT` info to
  the console (matching this client's own established "feedback prints to stdout" convention) and
  binds **O** (Overcharge) / **B** (Brace) during `CS_COMBAT` for a live human player to actually
  respond to a break with a real, timed keypress. `--selftest` never presses either (same neutral
  default as the test tool above), so the existing hand-derived selftest smoke test is unaffected.
- `scripts/build.sh` — a brand NEW smoke test proves the comeback claim end to end over the real
  wire protocol, not just in the pure-logic unit tests: two **identical** loadouts
  (Generator+Conductor+Railgun+Bulwark, the same loadout the original D2 smoke test already uses)
  fire in perfect lockstep and, left alone, hand-derive to an **exact tie at tick 24** (both hulls
  hit 0% simultaneously — 20 armor each absorbs the tick-3/tick-6 shots, then 15 dmg/hit on ticks
  9/12/15/18/21/24 drains both from 100% to 0% on the same tick). Scripting ONLY one side
  (`--round-call 2:overcharge`) to call Overcharge at round-break #2 (after tick 10, hull 75%/75%
  — still exactly even, so this exercises the BASELINE non-behind 1.5× payoff over the wire; the
  behind-amplified 2.0× value is pinned separately by the unit test, which doesn't need a live
  network race to hit an exact "behind" hull differential) and sleeping for the server's own
  `target_ms` turns that tie into a real win, three ticks early:
  - round 3 (ticks 11-15, dmg_mult=1.5×): tick 12 deals 22.5 (75→52.5), tick 15 deals 22.5
    (52.5→30); the untouched opponent still deals 15/hit back (75→60→45)
  - round 4 (ticks 16-20, back to neutral): tick 18, 15/15 (30→15 / 45→30)
  - round 5: tick 21, 15/15 — the boosted side's opponent hits **0% and dies**; the boosted side
    is still at 15% → **win at tick 21**, where the untouched baseline would still be an even tie
  - Verified for real, not just asserted: `build/dw2_round_a.log` shows the scripted
    `ROUND_CALL round=2 call=overcharge`, a `ROUND_RESULT your_grade=3` (PERFECT), and
    `MATCH_END result=1 reason=0 ticks=21`; `build/dw2_round_b.log` shows
    `MATCH_END result=0 reason=0 ticks=21` on the other side of the same match.
- Full `scripts/build.sh` stays green end to end (ASan+UBSan across every binary) — ran clean
  locally with this change: 96 core-loop checks, the original D2 smoke test (now correctly
  unaffected by round-breaks), the new comeback smoke test, and the `dw2_client` smoke test, in
  that order.

## Real, named simplifications in this V0 (not oversights)

- **`apps/local/main.c`'s dummy fight is untouched — no round-break against the fixed dummy.**
  Bluffing requires a live opponent to bluff against; a fixed, non-adaptive dummy makes the call
  layer meaningless (it can't read or react to anything), so this stays exactly the D1 mechanic it
  always was. If a round-break ever makes sense there, it's a separate, later decision.
- **Comeback amplification is unit-tested, not network-tested, for the "behind" branch
  specifically.** Constructing a live two-socket match that lands EXACTLY on a behind/ahead hull
  split at a chosen round boundary, on top of already-nontrivial real-time-skill-check scripting,
  adds a lot of fragile timing/ordering surface for comparatively little additional proof over what
  `test_round_overcharge_payoffs`/`test_round_brace_payoffs` already pin exactly and cheaply. The
  wire-protocol smoke test's job — proving the mechanism is really wired end to end (real
  server-measured timing, real grading, real combat-outcome change) — doesn't need the behind
  branch specifically to do that.
  * The wire-protocol smoke test could not use the A-vs-empty-grid loadout from the ORIGINAL D2
    smoke test to demonstrate a comeback, because an empty grid never deals any damage back — its
    opponent is never "behind" in any hull sense, there's nothing to come back FROM. A second,
    identical-loadout matchup was written specifically so both sides start genuinely contested.
- **A single call is accepted per round-break per seat; there is no "take it back" or re-roll.**
  The first `DW2_C_ROUND_CALL` a seat sends this break wins; a second is silently ignored server-
  side (`handle_round_call`). Matches this repo's own "fail clean, no partial state" discipline
  elsewhere (e.g. Panic Cut).
- **The interactive `dw2_client`'s O/B keybinding has no visual timer/target UI** — the target_ms
  and remaining budget are printed to the console, same bare "colored rectangles + stdout feedback"
  honesty this client already carries for the rest of combat (`NORTHSTAR.md`'s own D2 section). A
  real on-screen countdown bar is real, deferred UI polish, not attempted here.
- **`DW2_ROUND_TICKS`/target range/grading windows/payoff numbers are a first playable pass, not
  a balance-tested final table.** They were chosen to be provably testable (localhost round-trip
  jitter comfortably inside an 80ms PERFECT window; a full match's worth of round-breaks stays
  well within CI's real wall-clock patience) and to make the comeback/bluff claims mechanically
  real, not to be a tuned, played-in-anger balance pass — that needs real playtesting this V0 makes
  possible for the first time, not more solo design iteration.

## Deferred (not attempted this pass)

- **Real playtesting and balance tuning** of the target-ms range, grading windows, and payoff
  magnitudes — this V0's whole job was proving the mechanism is real and wired correctly, not
  tuning a feel that only reveals itself against a live human opponent.
- **A richer bluff layer** — e.g. letting a player see a PARTIAL signal about their opponent's
  historical call pattern (a real "tell" a sharp player could exploit), or more than two call
  options. Two calls with a clean risk/reward asymmetry was the real, buildable V0 cut; N-call
  rock-paper-scissors-shaped systems are a natural, later extension once two-call is proven fun.
- **SECTION 549's "cannon programming" (LO integration into D2's weapon-fire decision logic)** was
  explicitly held pending this section landing first (BACKLOG's own note: "wiring a new weapon-
  decision hook into code that's about to be restructured would be wasted/conflicting work"). This
  section's restructuring of `dw2_ship_tick`'s damage path (`dmg_mult`) landed stable first, then
  SECTION 549 itself landed: a real, compiled LO program now makes the fire/hold call every tick,
  built and live-verified — see `docs/LO_CANNON_PROGRAMMING.md`.
- **Round-break interaction with `apps/local/main.c`'s dummy fight or a future real bot (D4)** — a
  bot needs its own decision logic for the Overcharge/Brace/timing choice, which doesn't exist yet
  (D4 itself is still a named, not-yet-built phase in `NORTHSTAR.md`).
- **UI polish for `dw2_client`'s round-break moment** (a visual countdown, an on-screen call
  picker) — named above, real, deferred.
