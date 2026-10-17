/* DEADWEIGHT_2 ship state + energy routing + combat resolution -- ruleset 2.
 *
 * THE FIGHT, in the order a second plays out (dw2_match_tick):
 *  1. POWER. Each ship's generators put out watts; power flows through wires, is divided at splitters, scaled by amplifiers and resistors, banked by
 *     capacitors / batteries / delayers, gated by transistors (the circuit rules of ruleset 1, unchanged). A weapon charges on the power that reaches it. A
 *     closed loop of wire grows Back-EMF and burns out. A weapon that has reached its threshold fires (up to 3 shots a second).
 *  2. FIRE. Every ship's shots are decided before any lands (a gun that is destroyed this second still fires this second). A shot flies down a LANE: the
 *     enemy's column of the same number, bow (row 0) to stern. What it does there depends on the weapon:
 *       Pulse Laser  needs a CLEAR LANE on its own ship (nothing alive in front of it) or it holds fire. Hits the FIRST part in the enemy lane. Cannot be dodged.
 *       Railgun      fires through its own ship. Hits the first part in the lane and what it did not spend CARRIES ON (x0.8) to the next, and so on to the hull.
 *       Siege Cannon fires over everything. The shell lands on the DEEPEST part in the lane (the stern end), whatever stands in front of it, and the blast hurts what touches it.
 *     Damage to a part is the weapon's damage times the part's multiplier for that weapon (Bulwark: lasers x0.5. Titanium: slugs and shells x0.5. Aether Ore:
 *     lasers x0.2, shells x1.5). A part at 0 HP is destroyed: it no longer conducts, fires, heals or blocks. A shot that finds nothing in its lane hits the hull
 *     for DW2_HULL_FACTOR of its damage. A Brace shield soaks damage before anything else. Rails and cannon shells can be DODGED: the lighter the ship, the more.
 *  3. UPKEEP. Heat from a Quantum Core burns every uncooled neighbour; Xeno-Biomass grows back the parts it touches; an Aegis Emitter repairs the weakest part
 *     in its 3x3; a Mender mends hull. Win: enemy hull at 0, else after DW2_MATCH_TIMEOUT_TICKS the higher hull, then the larger share of its structure still standing, then cost.
 *
 * Everything that happens is also written to a Dw2EventLog (fire, hit, hull, dodge, shield, heal, repair, heat, burn) so a client can animate it.
 *
 * Rules per DEADWEIGHT/docs/SPEC_REVIEW.md still in force: §2 win condition, §3 one-to-many energy routing only (a second feed into a fed cell is inert), the
 * Back-EMF meltdown of closed loops.
 */
#ifndef DW2_COMBAT_H
#define DW2_COMBAT_H

#include "items.h"

#define DW2_GENERATOR_OUTPUT      10.0f
#define DW2_BACK_EMF_GROWTH       1.35f   /* per-tick multiplier while a loop persists */
#define DW2_BACK_EMF_SHATTER_MULT 2.5f    /* shatters past this many multiples of GENERATOR_OUTPUT */
#define DW2_MATCH_TIMEOUT_TICKS      30   /* "a 30-second combat limit" -- 1 tick = 1 second */
#define DW2_AMPLIFIER_GAIN         1.5f   /* power passing through an Amplifier is multiplied by this */
#define DW2_GENERATOR_VOLTS        5.0f   /* a Generator is 5 V x 2 A = 10 W; an Amplifier raises the volts, a Resistor cuts the amps */
#define DW2_CAP_FULL              30.0f   /* a Capacitor releases everything it holds in one tick once it holds this much */
#define DW2_CAP_MAX               60.0f
#define DW2_BAT_START             45.0f   /* a Battery goes into the fight half charged ... */
#define DW2_BAT_MAX               90.0f
#define DW2_BAT_RATE              15.0f   /* ... and gives up to this much a tick */
#define DW2_GATE_MIN               2.0f   /* a Transistor's gate must have been fed this much on the previous tick for the channel to conduct */
#define DW2_POWER_MAX            120.0f   /* no single feed carries more than this (stops a ring of Amplifiers running away) */
#define DW2_FLOW_BUDGET           4096    /* hand-offs one tick may make; the walk is deterministic, so every implementation stops at the same place */
#define DW2_MAX_SHOTS_PER_TICK       3    /* a weapon fed more than its threshold three times over still fires three times a second */
#define DW2_RAIL_CARRY            0.8f    /* a slug keeps this fraction of what it did not spend when it goes on through the next part */
#define DW2_CANNON_SPLASH         0.5f    /* the parts touching the one a shell lands on take this fraction of it */
#define DW2_HULL_FACTOR           0.5f    /* a shot that reaches the hull does this fraction of its damage to it */
#define DW2_MENDER_HEAL            2.0f   /* hull per tick, per 10 W fed to a Mender (never past 100) */
#define DW2_AEGIS_REPAIR           4.0f   /* HP per tick, per 10 W fed to an Aegis Emitter, to the weakest part in its 3x3 */
#define DW2_BIOMASS_HEAL_OTHER     2.0f   /* HP a Xeno-Biomass cell grows back on each touching cell of another part, each tick ... */
#define DW2_BIOMASS_HEAL_OWN       1.0f   /* ... and on each touching cell of its own colony */
#define DW2_EVADE_PER_TON          1.5f   /* % chance to dodge a slug or shell, per ton under the weight budget (capped) */
#define DW2_EVADE_MAX             50.0f

/* What a weapon item fires: it fires when its charge reaches `threshold` (it gains 10 x the gain of the path that feeds it each tick), for `damage` raw damage.
 * kind: 1 laser, 2 rail, 3 cannon. threshold 0 = not a weapon. */
enum { DW2_SHOT_NONE = 0, DW2_SHOT_LASER = 1, DW2_SHOT_RAIL = 2, DW2_SHOT_CANNON = 3 };
typedef struct { float threshold, damage; int kind; } Dw2WeaponStats;
Dw2WeaponStats dw2_weapon_stats(int item_id);
/* the fraction of the power a Resistor item lets through (1R 0.75, 2R 0.5, 4R 0.25); 1 for anything else */
float dw2_resistor_factor(int item_id);
/* A Capacitor / Battery / Delayer / Transistor is DIRECTED by how it is rotated: its input is the west side turned `rotation` quarter turns, its
 * output the east side turned the same, and a Transistor's gate the north side turned the same. */
uint8_t dw2_rotated_port(uint8_t port, int rotation);

/* ---- the event log: everything a fight does, in order ---- */
enum {
    DW2_EV_FIRE = 1,    /* side=shooter a=weapon cell b=item c=lane d=1 if boosted */
    DW2_EV_HIT,         /* side=target a=cell b=damage to the cell c=its HP after d=flags e=weapon item */
    DW2_EV_HULL,        /* side=target a=hull damage b=hull after c=weapon item d=lane */
    DW2_EV_EVADE,       /* side=target a=lane b=weapon item */
    DW2_EV_SHIELD,      /* side=target a=absorbed b=shield after c=weapon item d=lane */
    DW2_EV_HEAL,        /* side=owner a=mender cell b=hull healed (tenths) c=hull after */
    DW2_EV_REPAIR,      /* side=owner a=cell repaired b=HP c=its HP after d=source cell e=0 aegis 1 biomass */
    DW2_EV_HEAT,        /* side=owner a=cell b=damage c=its HP after d=1 if destroyed e=source cell */
    DW2_EV_BURN         /* side=owner a=cell burned out by Back-EMF */
};
enum { DW2_HITF_DESTROYED = 1, DW2_HITF_RESISTED = 2, DW2_HITF_WEAK = 4, DW2_HITF_PIERCED = 8 };
typedef struct { uint8_t type, side, a, b, c, d, e; } Dw2Event;
#define DW2_EVENT_MAX 60
typedef struct { Dw2Event ev[DW2_EVENT_MAX]; int n; int dropped; } Dw2EventLog;

typedef struct {
    int item_id;                 /* index into dw2_catalog, -1 = empty slot */
    int anchor_row, anchor_col, rotation;
    int cut;                      /* unused (ruleset 2 has no cut-in-place: see dw2_split_pieces) */
    Dw2PlacedCell cells[DW2_MAX_ITEM_CELLS];
    int cell_n;
    dw2_mask mask;
} Dw2Placement;

typedef struct { dw2_mask cells; float phi; int active; } Dw2LoopTracker;

typedef struct {
    int idx;                                /* 0 or 1: which ship of the match this is (the `side` of the events it makes) */
    Dw2Grid grid;
    Dw2Placement placements[DW2_MAX_PLACEMENTS];
    int placement_n;
    int cost, weight;                       /* of everything placed so far */
    dw2_mask dead;                          /* destroyed cells: shot, burnt by heat or by Back-EMF */
    dw2_mask burned;                        /* ... of which by Back-EMF */
    float cell_hp[DW2_GRID_CELLS];          /* current HP of each cell (0 where empty or destroyed) */
    float cell_max[DW2_GRID_CELLS];         /* full HP of each cell (0 where empty) */
    float weapon_charge[DW2_MAX_PLACEMENTS]; /* indexed by placement slot */
    int shots[DW2_MAX_PLACEMENTS];          /* the volley this tick */
    float shot_dmg[DW2_MAX_PLACEMENTS];
    float pin[DW2_GRID_CELLS];              /* power that reached each cell this tick (what the support parts run on) */
    Dw2LoopTracker loop;
    float hull_pct;
    float shield;                           /* a Brace shield: soaks damage before any part does */
    float evade_pct, evade_acc;             /* the chance to dodge a slug or shell, as a running tally: at 100 one is dodged */
    int cargo_value;
    float cell_q[DW2_GRID_CELLS];           /* stored charge of a Capacitor / Battery cell; a Delayer's newest tick */
    float cell_q2[DW2_GRID_CELLS];          /* a Delayer's older tick */
    uint8_t gate_open[DW2_GRID_CELLS];      /* a Transistor whose gate was fed on the previous tick */
    int shatter_events;                     /* test-observable: how many times Back-EMF has fired */
    float dmg_mult;                         /* multiplies this ship's OWN weapon damage output for
                                              * the current round of ticks -- 1.0 = neutral. Set by
                                              * a graded round-break call (core/round.h); a ship
                                              * that doesn't call, or calls Brace, stays at 1.0. */
} Dw2Ship;

typedef enum { DW2_RESULT_ONGOING = 0, DW2_RESULT_SELF_WIN, DW2_RESULT_ENEMY_WIN, DW2_RESULT_TIE } Dw2MatchResult;

void dw2_ship_init(Dw2Ship *s);
/* Places the part. Returns the placement index, or -1 if illegal (off the grid, overlapping, over the credit or weight budget, or no slot). */
int dw2_ship_place(Dw2Ship *s, int item_id, int anchor_row, int anchor_col, int rotation);
/* Locks in the grid: computes starting HP, weight-based evasion and cargo value. Call once, after packing, before the first tick. */
void dw2_ship_start_combat(Dw2Ship *s);

/* One second of the whole fight: both ships' power, both volleys, both upkeeps. `tick` is the 0-based second; it decides whose volley lands first. */
void dw2_match_tick(Dw2Ship *a, Dw2Ship *b, int tick, Dw2EventLog *log);
/* One ship's half of a second (power, its volley onto `enemy`, its upkeep), for tests and for checking a port of the circuit. */
void dw2_ship_tick(Dw2Ship *self, Dw2Ship *enemy, Dw2EventLog *log);
/* Evaluates the match result from `self`'s point of view at the given elapsed tick count. */
Dw2MatchResult dw2_match_result(const Dw2Ship *self, const Dw2Ship *enemy, int elapsed_ticks);

/* ---- readers (used by the server, the bot search, the tests and mirrored by the web client) ---- */
float dw2_ship_structure(const Dw2Ship *s);            /* total HP of the cells still standing */
float dw2_ship_structure_max(const Dw2Ship *s);
float dw2_ship_standing_score(const Dw2Ship *s);       /* hull + shield + structure/8: who is ahead at a round break */
int dw2_cell_alive(const Dw2Ship *s, int cell);
/* Is the laser at cell `cell` free to fire (nothing alive in front of it on its own ship)? */
int dw2_lane_clear_ahead(const Dw2Ship *s, int cell);
/* Is this cell cooled (a Cryo-Vent cell, or touching a living one)? and how many degrees of heat does it take (2 per touching Core cell, 0 if cooled)? */
int dw2_cell_cooled(const Dw2Ship *s, int cell);
int dw2_cell_heat(const Dw2Ship *s, int cell);
/* The damage multiplier of a weapon kind (DW2_SHOT_*) against the part at `cell`. */
float dw2_cell_mult(const Dw2Ship *s, int cell, int kind);
/* The weapon boost a weapon placement gets from the Titanium it touches (1.0 if none), and the repair boost an Aegis cell gets from touching Aether. */
float dw2_weapon_boost(const Dw2Ship *s, int placement);
float dw2_aegis_boost(const Dw2Ship *s, int cell);
/* How many shots of a weapon kind (DW2_SHOT_*) it takes to destroy everything standing in a lane (the pack screen's "shots to open" table). */
#define DW2_LANE_SHOTS_MAX 60
int dw2_lane_shots_to_open(const Dw2Ship *s, int lane, int kind);

#endif
