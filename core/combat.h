/* DEADWEIGHT_2 ship state + energy routing + combat resolution -- Phase D1.
 * Rules per DEADWEIGHT/docs/SPEC_REVIEW.md:
 *   §2 win condition: hull% is primary (0% = loss); 30s/tick timeout -> remaining hull% tiebreak,
 *      cargo value secondary tiebreak.
 *   §3 energy routing: one-to-many (Splitter) is legal; many-to-one is not -- a second incoming
 *      connection into an already-fed cell is simply inert (no damage, no explosion).
 *   Back-EMF: a closed wiring loop accumulates Phi(t) = Phi0 * e^(k*t); shatters (burns out,
 *      permanently non-conducting) past 2.5*Phi0. Implemented here as a per-tick multiplicative
 *      growth on a single tracked loop mask -- V0 simplification: multiple simultaneous distinct
 *      bad loops are tracked as one combined region rather than independently (a real, named
 *      simplification, not an oversight -- see combat.c's own doc comment on dw2_ship_tick).
 */
#ifndef DW2_COMBAT_H
#define DW2_COMBAT_H

#include "items.h"

#define DW2_GENERATOR_OUTPUT      10.0f
#define DW2_BACK_EMF_GROWTH       1.35f   /* per-tick multiplier while a loop persists */
#define DW2_BACK_EMF_SHATTER_MULT 2.5f    /* shatters past this many multiples of GENERATOR_OUTPUT */
#define DW2_WEAPON_CHARGE_THRESHOLD 30.0f
#define DW2_WEAPON_DAMAGE          15.0f  /* hull % per shot */
#define DW2_ARMOR_PER_ARMOR_ITEM   20.0f  /* Bulwark Plate's own flat contribution */
#define DW2_ARMOR_PER_DEAD_CELL     8.0f  /* "waste becomes armor" (NORTHSTAR.md) */
#define DW2_MATCH_TIMEOUT_TICKS      30   /* "a 30-second combat limit" -- 1 tick = 1 second */

typedef struct {
    int item_id;                 /* index into dw2_catalog, -1 = empty slot */
    int anchor_row, anchor_col, rotation;
    int cut;                      /* 0 = whole shape placed, 1 = fragment set placed */
    Dw2PlacedCell cells[DW2_MAX_CUT_CELLS];
    int cell_n;
    dw2_mask mask;
} Dw2Placement;

typedef struct { dw2_mask cells; float phi; int active; } Dw2LoopTracker;

typedef struct {
    Dw2Grid grid;
    Dw2Placement placements[DW2_MAX_PLACEMENTS];
    int placement_n;
    dw2_mask burned;                        /* permanently shattered by Back-EMF */
    float weapon_charge[DW2_MAX_PLACEMENTS]; /* indexed by placement slot */
    Dw2LoopTracker loop;
    float hull_pct;
    float armor;
    int cargo_value;
    int shatter_events;                     /* test-observable: how many times Back-EMF has fired */
} Dw2Ship;

typedef enum { DW2_RESULT_ONGOING = 0, DW2_RESULT_SELF_WIN, DW2_RESULT_ENEMY_WIN, DW2_RESULT_TIE } Dw2MatchResult;

void dw2_ship_init(Dw2Ship *s);
/* Places the item's WHOLE (uncut) shape. Returns the placement index, or -1 if illegal/full. */
int dw2_ship_place(Dw2Ship *s, int item_id, int anchor_row, int anchor_col, int rotation);
/* Replaces a placement's shape with its authored fragment set (SPEC_REVIEW.md §1). Returns 1 on
 * success, 0 if the item is unsplittable, already cut, or the fragments don't fit (grid unchanged
 * on failure -- fail clean, not partially). Vacated cells become Ruined Real Estate; newly exposed
 * Dead Square cells grant immediate bonus armor. Legal to call mid-combat. */
int dw2_ship_panic_cut(Dw2Ship *s, int placement_idx);
/* Locks in the grid: computes starting hull/armor/cargo value. Call once, after packing, before
 * the first dw2_ship_tick(). */
void dw2_ship_start_combat(Dw2Ship *s);
/* One energy-routing + weapon-fire tick for `self`; any weapon fire damages `enemy`. */
void dw2_ship_tick(Dw2Ship *self, Dw2Ship *enemy);
/* Evaluates the match result from `self`'s point of view at the given elapsed tick count. */
Dw2MatchResult dw2_match_result(const Dw2Ship *self, const Dw2Ship *enemy, int elapsed_ticks);

#endif
