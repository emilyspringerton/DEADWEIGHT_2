# D2 Cannon Programming — a real, compiled LO decision function in the live combat loop

`EMILY/BACKLOG.md` SECTION 549. Founder real-time, 2026-09-25, across three messages: "add in LO
programming to actual gameplay of DEADWEIGHT add stdlibs to LO whatever is needed that is
missing" → "like cannon programming or something" → "make LO work both left to right and bottom
to top... its like lisp the actual programming language is data." Routed via `emily observe` (obs
`2026-09-25T10-46-42Z`, Apple #20808) per `EMILY/docs/THE_EMILY_WAY.md` Principle 18, logged into
BACKLOG SECTION 549, then worked — Backlog First / Apple Before Mark-Done order. The 2D
bidirectional execution model half of that same founder message is a separate, already-completed
design-only pass (`LO/LO_2D_NORTHSTAR.md`); this doc is the OTHER half — the concrete "cannon
programming... stdlib gaps" ask — and is real, built, live-verified code, not a plan.

## Assumption named, not assumed silently

BACKLOG SECTION 549 itself already flags this: "founder said 'DEADWEIGHT,' but the concrete
'cannon' idea maps far more literally onto **D2's** Railgun/energy-routing weapon-fire decision
logic (`core/combat.h`'s `dw2_ship_tick`) than onto DEADWEIGHT proper (a card game with nothing
cannon-shaped)." `NORTHSTAR.md`'s own Deferred section independently reaches the same conclusion.
This doc proceeds on that reading — D2, not DEADWEIGHT proper — for the same reason SECTION 548
proceeded on an analogous flagged-not-confirmed D2 reading: the ambiguity is named here, in the
BACKLOG item, and in `NORTHSTAR.md`, not silently resolved by omission. If that reading is wrong,
this is real, working infrastructure that would need re-scoping onto DEADWEIGHT's own turn-based
round structure — a different design problem — not a wasted V0.

## LO stdlib gaps — audited, not assumed

The founder's ask bundles two asks together ("add stdlibs to LO whatever is needed that is
missing" + "cannon programming"). Checked directly against `LO/NORTHSTAR.md` and `LO/GRAMMAR.md`
rather than assumed: LO's own real, already-named stdlib gaps (`STACK`/`MATMUL`/matrix
construction, the PCRE-lite pattern matcher) are for vector/matrix/pattern-matching programs —
**genuinely unrelated to what a weapon-fire decision function needs.** The real, current LO
compiler already has everything this feature requires: `DOOR I32` (an exported, typed return
value), a bare top-level `LAMBDA` compiling to a real, externally-callable, runtime-parameterized
`defn` (`LO/NORTHSTAR.md`'s own "Gap #2 — FIXED (2026-09-07)"), and `SWITCH`/`CASE`/`DEFAULT` over
a bare 4-state value. **Real, honest finding: zero new LO stdlib work was needed for this V0.**
The stdlib gaps LO's own NORTHSTAR names remain real and open for whatever future LO work
actually needs matrices or pattern matching — this feature just isn't that work.

## LO's real capability ceiling — checked before designing anything, not assumed

`LO/NORTHSTAR.md`'s own "Real, current blocker for DUNG integration" section is the load-bearing
precedent here: **LO's arithmetic domain is mod-4, not general integer arithmetic** — `Arith`'s
operator set (`PLUS4`/`MINUS4`/`AND4`/`OR4`/`XOR4`) operates over a 4-symbol (2-bit) base4 state
space by design, not as an oversight. A DUNG feature needing real, unbounded I32 division/modulo
over pixel widths was a genuine domain mismatch; only a slice "naturally 4-valued" (the DUNG doc's
own example: which of Left/Right/Up/Down was pressed) is a real fit via `SWITCH`/`CASE` today.
D2's weapon-fire decision has to be designed around that exact same ceiling, not around what would
be convenient in C.

Verified directly in this pass, not just read about: `go test ./...` in a real `LO` checkout with
a real, built `PARENA` binary (`make build` in a sibling `PARENA` checkout) passes clean —
`internal/emitter`'s own real end-to-end tests, including
`TestEmitTopLevelLambdaIsCallableWithARealRuntimeArgument`, actually compile LO source through
`lo build` → `parena build` → `cc` → execution and check the real returned value. This doc's own
`cannon_decision.llll`/`cannon_bank_on_safe_lead.llll` were verified the identical way (below),
not assumed to work by analogy.

## The real design: one naturally-4-valued input, one 2-valued output

`dw2_ship_tick`'s weapon-fire loop, pre-existing: every tick, every placement whose
`weapon_charge` has crossed `DW2_WEAPON_CHARGE_THRESHOLD` fires unconditionally. There is no
decision anywhere in it. This is the real "cannon" (a placed Railgun) and the real place a
decision hook belongs.

**The input.** LO's `Lambda`/`Call` are single-parameter in this v0 (`GRAMMAR.md` §2's own note).
Two live combat booleans `dw2_ship_tick` already has on hand pack into one real, naturally-4-valued
state, matching the DUNG precedent's own shape exactly (a 4-way `SWITCH` over a bare 2-bit value,
no arithmetic needed):

- **bit 0 — `behind`**: `self->hull_pct < enemy->hull_pct`, the exact same signal
  `core/round.h`'s own comeback grading already computes (SECTION 548) — reused, not reinvented.
- **bit 1 — `shot_is_overkill`**: `enemy->hull_pct <= DW2_WEAPON_DAMAGE * self->dmg_mult` — would
  this specific shot, at its real current damage (already accounting for a round-break Overcharge
  buff), finish the enemy.

Four real, meaningfully distinct states: safely ahead + non-lethal (state 0), behind + non-lethal
(state 1), ahead + lethal (state 2), behind + lethal — the comeback kill (state 3).

**The output.** `DW2_CANNON_FIRE` (1) or `DW2_CANNON_HOLD` (0) — `core/cannon.h`. A `HOLD` simply
skips this tick's fire check; `weapon_charge` is untouched and re-evaluated fresh next tick.

**Named, not assumed: a shot's own armor absorption isn't modeled in `shot_is_overkill`.** The
check compares against the enemy's raw `hull_pct`, not the post-armor damage the shot would
actually land (`dw2_ship_apply_damage` absorbs into armor first). A real, honest simplification —
computing the exact post-armor lethality would need threading armor state through this check too,
real, separate, and not needed for this V0's own acceptance bar (a real, testable, live-wired
decision hook).

## The real build pipeline: LO → `.prn` → PARENA → committed C

`cannon/cannon_decision.llll` is the real source of record:

```
🚪 🔢 💠 🔘 🧲 🔹 🌑 🌒 🔹 🌒 🌒 🔹 🌓 🌒 🔸 🌒;
```

Read: `DOOR I32` (exported I32 return) `LAMBDA` (`SWITCH` on the parameter: `CASE S0 → S1`,
`CASE S1 → S1`, `CASE S2 → S1`, `DEFAULT → S1`) — FIRE in every one of the 4 real states.

`scripts/generate_cannon.sh` (new, mirroring `scripts/generate_construct.sh`'s own "commit the
generated output, regenerate via a documented script" precedent for Principle 21) runs the real
pipeline: `lo build cannon_decision.llll -o cannon_decision.prn`, then
`parena build stdlib/base4/algebra.prn cannon_decision.prn -o cannon_decision_gen.c`. Both
`cannon/cannon_decision.prn` and `cannon/cannon_decision_gen.c` are committed — machine output, "do
not edit by hand" per their own generated header comments, same discipline every other generated
file in this monorepo follows. **This is a deliberate, real architectural choice, not an
oversight**: it means `scripts/build.sh` and `.github/workflows/ci.yml` never need a sibling
`LO`/`PARENA` checkout at all — only `scripts/generate_cannon.sh` does, and only when a `.llll`
source actually changes. This keeps D2's own "no external dependencies for the core loop or
`dw2_server`" claim honest at ordinary build time, the same way an already-vendored,
already-committed dependency (`core/net.h`/`http.{h,c}`/`iduna.{h,c}`, ported from DEADWEIGHT) does
today — this is that same precedent, applied to a generated-from-source dependency instead of a
hand-copied one.

`core/parena_runtime.h`/`.c` are vendored, unmodified, from `PARENA/runtime/` (public domain,
Unlicense — `PARENA/LICENSE`; this vendors one file under that upstream license, it does not add
the Unlicense to this repo as a whole, which `CLAUDE.md` still names as deliberately unresolved).
The generated C `#include`s this header unconditionally; **`PARENA_NO_GRAPHICS`** (defined in
`scripts/build.sh`'s own `CFLAGS_BASE`, the header's own documented escape hatch for "a real
target with zero SDL2 in its own `.prn` source") skips the header's `SDL2`/`SDL2_ttf` include
entirely — verified directly: `core/parena_runtime.h` compiles clean under D2's exact
`-std=c99 -Wall -Wextra -Werror` with ASan+UBSan with no SDL2 symbols pulled in, keeping
`dw2_server`/`test_core_loop` genuinely SDL-free, matching their own pre-existing "no external
dependencies" claim.

`core/cannon.h`/`.c` is the thin, hand-written C seam: packs the two booleans into the real I32
state and calls the generated, exported `lo_program`. `core/combat.c`'s `dw2_ship_tick` calls
`dw2_cannon_decide` once per charged weapon per tick, in place of the old unconditional fire.

## Why the shipped default is deliberately the boring answer

The shipped `cannon_decision.llll` is FIRE in all 4 states — **byte-identical to the exact
pre-cannon-programming behavior.** This is a deliberate, real V0 cut, not a missed opportunity:
holding a charged weapon has **no compensating mechanical benefit** in the current model (damage
is a fixed `DW2_WEAPON_DAMAGE * dmg_mult` regardless of how long charge has sat above threshold —
there is no charge cap or burst-on-release mechanic). A policy that holds fire whenever safely
ahead therefore has a real, honestly-discovered failure mode: against an opponent that can never
put it "behind" (e.g. an unarmed target) and never drops into overkill range, it holds forever and
never wins on hull at all. Verified directly, not asserted — see `tests/test_cannon_hold.c`'s own
scenario below. Shipping that as the LIVE default would be a real regression (some matches
stall to the 30-tick timeout that used to resolve on hull well before it), not a strict
improvement, so it stays a second, real, standalone-tested example program instead
(`cannon_bank_on_safe_lead.llll`) proving the mechanism can express real conditional strategy,
without gambling the live game's own win condition on an unbalanced V0 policy. Giving `HOLD` a
real payoff (a charge cap, a bigger burst on release) is real, separate, deferred design work —
named below, not glossed over.

## Live-verified, not just compiled

- `test_core_loop.c`'s new `test_cannon_decide_always_fires` — direct, pure-C checks that
  `dw2_cannon_decide` returns `DW2_CANNON_FIRE` for all 4 real `(behind, overkill)` combinations
  against the shipped policy. 100 checks total (was 96 before this pass), 0 failures.
- **Every existing hand-derived smoke-test number in this repo is unchanged**: `dw2_local`'s own
  `--selftest` (`result=3 hull=0%/0% ticks=24`), the original D2 wire-protocol smoke test (A beats
  an empty grid), the round-break comeback smoke test (A wins at tick 21), and `dw2_client`'s own
  smoke test — all byte-identical to their pre-cannon-programming values, proving the shipped
  policy really is a behavioral no-op, not just claimed to be.
- **`tests/test_cannon_hold.c`** (new, standalone binary — see "why two binaries" below): links the
  REAL `cannon_bank_on_safe_lead.llll` program through the real `dw2_ship_tick` call path. An
  attacker with a charged Railgun facing an unarmed, full-hull target holds fire for two full ticks
  past the threshold (charge climbs 30 → 40 unspent, hull stays 100%, confirming a `HOLD` really
  does skip the fire branch and really does leave `weapon_charge` untouched for the energy-routing
  phase to keep feeding) — then, once the target's hull is set into this shot's own overkill range,
  the SAME compiled decision switches to `FIRE` on the very next tick and the shot lands. 8 checks,
  0 failures.
- **`tools/dw2_cannon_demo.c` / `build/dw2_cannon_demo`** (new): asserts `cannon_bank_on_safe_lead`'s
  real compiled output for all 4 states against the hand-designed table (HOLD only at state 0, FIRE
  at 1/2/3) — a second, independent proof the LO → PARENA → C pipeline isn't a one-off that only
  works for the trivial always-FIRE case.
- **Why two binaries, not one**: `parena build` always exports the compiled function as
  `lo-program` (`lo_program` in C); two generated files linked into the same binary would collide
  on that symbol (and on the shared `base4_*`/`symbol_*` helpers `(import base4/algebra)` also
  re-emits into each generated file). Namespacing multiple simultaneous compiled LO modules in one
  binary is real, separate, harder engineering — not attempted this pass, named honestly rather
  than worked around with a fragile rename hack. `scripts/build.sh` compiles
  `cannon_decision_gen.c` into every real D2 binary (`dw2_server`/`dw2_local`/`dw2_client`/
  `dw2_test_client`/`test_core_loop`) and `cannon_bank_on_safe_lead_gen.c` into two small, separate,
  test-only binaries instead.
- Full `scripts/build.sh` stays green end to end (ASan+UBSan across every binary), ran clean
  locally with this change, including the two new sections.

## Real, named simplifications and deferred work (not oversights)

- **Only one real, live decision function governs both ships.** No per-ship, per-item, or
  player-authored cannon programs — the whole grid's worth of weapons all consult the same,
  symmetric compiled function, matching the same "both sides get the identical real mechanic"
  precedent SECTION 548's round-break payoff table already established. A real plugin-style system
  (each placement running its own program, or a player picking one at pack time) is a real,
  separate, much larger follow-up, not attempted here.
- **`HOLD` has no real payoff in this V0** (named above, in depth) — a charge cap /
  bigger-burst-on-release mechanic is real, deferred design work, needed before `HOLD` could ever
  be safely promoted to the live default.
- **`shot_is_overkill` ignores armor absorption** (named above) — a real, honest simplification.
- **`apps/local/main.c`'s fixed dummy fight is unaffected** — it doesn't link `core/cannon.c` at
  all (its own compile line in `scripts/build.sh` is untouched), so the dummy keeps firing exactly
  as before. No real reason to route the dummy through a compiled decision function it can't
  meaningfully vary, mirroring SECTION 548's own identical call on the round-break mini-game.
  Should the dummy ever want cannon programming, wiring `$CANNON_SRC` into its own compile line is
  the whole change needed — the seam (`core/cannon.h`) is already real and ready for it.
- **No multi-module namespacing** (named above) — needed before more than one real LO decision
  program could ever coexist in the same live binary.
- **`scripts/generate_cannon.sh` needs a real, separately-built sibling `LO`/`PARENA` checkout** to
  regenerate `cannon/*.prn`/`cannon/*_gen.c` — undertaken only when a `.llll` source changes, never
  by ordinary `scripts/build.sh`/CI runs, which only ever compile the already-committed generated
  C. `.github/workflows/ci.yml` is unchanged by this pass for exactly that reason.

## Related

- `EMILY/BACKLOG.md` SECTION 549 — this doc's own originating backlog item.
- `LO/NORTHSTAR.md` / `LO/GRAMMAR.md` — the real, current LO compiler capability this design is
  built around, not assumed.
- `docs/COMBAT_REDESIGN.md` — SECTION 548, the round-break `behind` signal this design reuses
  directly for its own `behind` input bit.
- `NORTHSTAR.md` — D2's own overall architecture; this doc's own summary is folded into that doc's
  history section.
