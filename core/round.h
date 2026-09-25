/* DEADWEIGHT_2 round-break mini-game -- EMILY/BACKLOG.md SECTION 548 (D2 combat redesign).
 *
 * Assumption named, not assumed silently: the founder's "mini game in between ship auto battler
 * rounds... real time skill check... swingy... comeback mechanics... bluff strategies" is read as
 * targeting D2 specifically (the game with ships). See DEADWEIGHT_2/NORTHSTAR.md's own combat-
 * redesign section for the full rationale and DEADWEIGHT_2/docs/COMBAT_REDESIGN.md for the design
 * write-up this module implements.
 *
 * Round structure decision: combat is restructured into DW2_ROUND_TICKS-tick bursts ("rounds").
 * After each burst (except the very last, once the match timeout is reached), a ROUND BREAK pauses
 * ticking for a real-time skill check + a hidden simultaneous-reveal "call" -- this is the actual
 * "mini-game between rounds" the founder asked for, not a bolt-on stat roll.
 *
 * The skill check ("Surge Timing"): the server picks a deterministic target_ms (derived from the
 * match seed + round number, so a match is fully replayable and both sides see the SAME target --
 * no per-seat unfairness) and tells the player exactly what it is. The player must send their
 * ROUND_CALL as close to target_ms after receiving ROUND_BREAK as they can; the server grades the
 * REAL wall-clock gap it measured (not a client-reported timestamp -- server-authoritative,
 * un-cheatable). This is a genuine active timing/reaction input, not a passive dice roll.
 *
 * The bluff ("Overcharge vs. Brace call"): the call itself (aggressive/risky vs. safe/defensive)
 * is hidden from the opponent until DW2_S_ROUND_RESULT reveals both sides' calls simultaneously,
 * after both are locked in (or the deadline passes). A player can bait an opponent's read of their
 * pattern across rounds (always Bracing, then Overcharging when it matters) -- real hidden
 * information plus a real, consequential decision, on top of the wire protocol's own already-
 * hidden grid/placement detail (core/protocol.h's own doc comment).
 *
 * The comeback (swinginess): a ship that is BEHIND on hull% at the moment of grading gets an
 * amplified payoff for a successful call -- the losing side's own skill-check success matters
 * more, a real, mechanical (not "try harder") comeback lever. See dw2_round_grade()'s own table.
 *
 * This module is pure game logic (no networking, no SDL) -- deterministic and unit-testable in
 * tests/test_core_loop.c, same discipline as core/combat.c. */
#ifndef DW2_ROUND_H
#define DW2_ROUND_H
#include "combat.h"
#include <stdint.h>

#define DW2_ROUND_TICKS            5     /* ticks per combat round before a round-break */
#define DW2_ROUND_TARGET_MIN_MS    300
#define DW2_ROUND_TARGET_MS_SPAN   601   /* target_ms uniformly in [300, 900] */
#define DW2_ROUND_BUDGET_MS        1200  /* real wall-clock ms after ROUND_BREAK a call is accepted */
#define DW2_ROUND_PERFECT_MS       80    /* |elapsed - target| <= this -> PERFECT */
#define DW2_ROUND_GOOD_MS          220   /* <= this (and > PERFECT) -> GOOD; beyond -> MISS */
#define DW2_ROUND_OVERCHARGE_BACKFIRE_HULL 8.0f /* self hull%, OVERCHARGE MISS only */

enum { DW2_CALL_OVERCHARGE = 0, DW2_CALL_BRACE = 1, DW2_CALL_NONE = 2 };
enum { DW2_GRADE_NONE = 0, DW2_GRADE_MISS = 1, DW2_GRADE_GOOD = 2, DW2_GRADE_PERFECT = 3 };

/* Deterministic per-round target press time (ms since ROUND_BREAK was sent), derived from the
 * match seed and round number -- both sides are told the same value, so a match's mini-game
 * outcomes are fully reproducible given its seed and the calls made. */
uint16_t dw2_round_target_ms(uint32_t match_seed, int round_no);

/* The mechanical effect of one graded call, applied once to the calling ship at resolution. */
typedef struct { float dmg_mult; float armor_bonus; float self_damage; } Dw2RoundEffect;

/* Grades a call. `no_call`=1 means nothing arrived by the deadline: always DW2_GRADE_NONE with a
 * fully neutral effect (a player can't be punished -- or rewarded -- for a mini-game they never
 * engaged with; `call`/`elapsed_ms` are ignored in that case). `behind`=1 means this ship's hull%
 * was strictly lower than its opponent's at the moment of grading (the comeback trigger).
 * `*out_grade` receives the DW2_GRADE_* result. */
Dw2RoundEffect dw2_round_grade(uint32_t match_seed, int round_no, int call, int no_call,
                                uint32_t elapsed_ms, int behind, int *out_grade);

/* Applies a graded effect to a ship: sets its dmg_mult for the next round of ticks (reset to 1.0
 * by the next round-break's own dw2_round_grade result if this ship doesn't call again), adds any
 * armor bonus immediately (permanent, same convention as Panic Cut's dead-cell armor), and applies
 * any self-damage immediately (hull%, clamped at 0, same clamp dw2_ship_tick's own damage path
 * uses). */
void dw2_ship_apply_round_effect(Dw2Ship *s, Dw2RoundEffect e);

#endif
