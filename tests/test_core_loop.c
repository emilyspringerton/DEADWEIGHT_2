/* Headless core-loop tests (ruleset 2). No SDL dependency. Covers: placement and the credit/weight budget, splitting a part in place, the circuit (power, wires,
 * splitters, resistors, capacitor, battery, transistor, delayer, Back-EMF), and the fight: lanes, the three weapons (laser needs a clear lane, rail pierces,
 * cannon lands on the deepest part and splashes), what each material does to each weapon, shields, dodging, heat and cooling, Xeno-Biomass, Aegis repair,
 * Menders, destroyed parts cutting a circuit, the event log, the round-break mini-game and the wire codec. */
#include "../core/combat.h"
#include "../core/dummy.h"
#include "../core/round.h"
#include "../core/cannon.h"
#include "../core/protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)
#define NEAR(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.01f)
#define LASER 7.0f   /* a Pulse Laser shot's damage */

/* a target that never shoots back, with a hull big enough that nothing ends the fight */
static void foe_ship(Dw2Ship *f) { dw2_ship_init(f); f->idx = 1; }
static void ticks(Dw2Ship *me, Dw2Ship *foe, int n) { for (int t = 0; t < n; t++) dw2_ship_tick(me, foe, NULL); }
static int cell_of(int r, int c) { return dw2_cell(r, c); }
/* the HP of the part at (r,c) on a fighting ship */
static float hp(const Dw2Ship *s, int r, int c) { return s->cell_hp[cell_of(r, c)]; }

/* ---- placement, budget, splitting ---------------------------------------------------------------------------------------------------- */
static void test_placement_legality(void) {
    Dw2Ship s; dw2_ship_init(&s);
    CHECK(dw2_ship_place(&s, DW2_ITEM_GENERATOR, 0, 0, 0) == 0);
    CHECK(s.grid.occupied == dw2_bit(dw2_cell(0, 0)));
    CHECK(dw2_ship_place(&s, DW2_ITEM_CONDUCTOR, 0, 0, 0) == -1);          /* overlap */
    CHECK(s.placement_n == 1);
    CHECK(dw2_ship_place(&s, DW2_ITEM_TITANIUM_BEAM, 0, 4, 0) == -1);     /* cols 4..7: off the grid */
    CHECK(dw2_ship_place(&s, DW2_ITEM_CONDUCTOR, 0, 1, 0) == 1);
    CHECK(dw2_ship_place(&s, 99, 3, 3, 0) == -1 && dw2_ship_place(&s, -1, 3, 3, 0) == -1);
    CHECK(s.cost == dw2_catalog[DW2_ITEM_GENERATOR].cost + dw2_catalog[DW2_ITEM_CONDUCTOR].cost);
    CHECK(s.weight == dw2_catalog[DW2_ITEM_GENERATOR].weight + dw2_catalog[DW2_ITEM_CONDUCTOR].weight);
}

static void test_rotation_math(void) {
    uint8_t v = DW2_PORT_N;
    for (int i = 0; i < 4; i++) v = dw2_rotate_port_cw(v);
    CHECK(v == DW2_PORT_N);
    CHECK(dw2_rotate_port_cw(DW2_PORT_N) == DW2_PORT_E);
    CHECK(dw2_rotate_port_cw(DW2_PORT_E) == DW2_PORT_S);
    CHECK(dw2_rotate_port_cw(DW2_PORT_S) == DW2_PORT_W);
    CHECK(dw2_rotate_port_cw(DW2_PORT_W) == DW2_PORT_N);
    Dw2Cell line[2] = { { 0, 0, DW2_PORT_E, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE } };
    Dw2PlacedCell out[2];
    dw2_mask m = dw2_shape_cells(line, 2, 1, 2, 2, out);
    CHECK(m == (dw2_bit(dw2_cell(2, 2)) | dw2_bit(dw2_cell(3, 2))));
    CHECK(out[0].port == DW2_PORT_S);
}

static void test_budget(void) {
    /* credits: Railguns cost 240, so four fit under 1000 and the fifth does not */
    Dw2Ship s; dw2_ship_init(&s);
    int placed = 0;
    for (int i = 0; i < 6; i++) if (dw2_ship_place(&s, DW2_ITEM_RAILGUN, 0, i, 0) >= 0) placed++;
    CHECK(placed == 4);
    CHECK(s.cost == 4 * dw2_catalog[DW2_ITEM_RAILGUN].cost && s.cost <= DW2_BUDGET_CREDITS);
    CHECK(s.weight <= DW2_BUDGET_WEIGHT);
    /* a failed placement changes nothing */
    int cost = s.cost, weight = s.weight, n = s.placement_n; dw2_mask occ = s.grid.occupied;
    CHECK(dw2_ship_place(&s, DW2_ITEM_RAILGUN, 1, 0, 0) == -1);
    CHECK(s.cost == cost && s.weight == weight && s.placement_n == n && s.grid.occupied == occ);
    /* weight: Bulwark Plates are cheap (55) but weigh 5, so the 40 t budget stops the ninth */
    Dw2Ship w; dw2_ship_init(&w);
    int plates = 0;
    for (int r = 0; r < 6; r++) for (int c = 0; c < 6; c++) if (dw2_ship_place(&w, DW2_ITEM_BULWARK, r, c, 0) >= 0) plates++;
    CHECK(plates == DW2_BUDGET_WEIGHT / dw2_catalog[DW2_ITEM_BULWARK].weight);
    CHECK(w.cost < DW2_BUDGET_CREDITS && w.weight == DW2_BUDGET_WEIGHT);
    /* there is no part limit but the grid: 36 Conductors fit */
    Dw2Ship c; dw2_ship_init(&c);
    int cn = 0;
    for (int r = 0; r < 6; r++) for (int k = 0; k < 6; k++) if (dw2_ship_place(&c, DW2_ITEM_CONDUCTOR, r, k, 0) >= 0) cn++;
    CHECK(cn == 36 && c.placement_n == 36);
}

static void test_split_pieces(void) {
    for (int rot = 0; rot < 4; rot++) {
        /* a Titanium Beam splits into two Shards lying exactly on its four cells */
        Dw2Ship s; dw2_ship_init(&s);
        int beam = dw2_ship_place(&s, DW2_ITEM_TITANIUM_BEAM, rot % 2 ? 0 : 2, rot % 2 ? 2 : 0, rot);
        CHECK(beam == 0);
        dw2_mask whole = s.placements[0].mask;
        Dw2SplitPiece pc[4];
        int n = dw2_split_pieces(DW2_ITEM_TITANIUM_BEAM, rot, s.placements[0].anchor_row, s.placements[0].anchor_col, pc);
        CHECK(n == 2);
        Dw2Ship t; dw2_ship_init(&t);
        int ok = 1; for (int k = 0; k < n; k++) ok &= dw2_ship_place(&t, pc[k].item, pc[k].row, pc[k].col, pc[k].rotation) >= 0;
        CHECK(ok && t.grid.occupied == whole);
        /* Aether Ore into four Chips on its four cells */
        Dw2Ship o; dw2_ship_init(&o);
        dw2_ship_place(&o, DW2_ITEM_AETHER_ORE, 1, 1, rot);
        n = dw2_split_pieces(DW2_ITEM_AETHER_ORE, rot, 1, 1, pc);
        CHECK(n == 4);
        Dw2Ship u; dw2_ship_init(&u);
        ok = 1; for (int k = 0; k < n; k++) ok &= dw2_ship_place(&u, pc[k].item, pc[k].row, pc[k].col, pc[k].rotation) >= 0;
        CHECK(ok && u.grid.occupied == o.grid.occupied);
    }
    Dw2SplitPiece pc[4];
    CHECK(dw2_split_pieces(DW2_ITEM_RAILGUN, 0, 0, 0, pc) == 0);
    /* the tax: the pieces cost and weigh more than the whole */
    CHECK(2 * dw2_catalog[DW2_ITEM_TITANIUM_SHARD].cost > dw2_catalog[DW2_ITEM_TITANIUM_BEAM].cost);
    CHECK(2 * dw2_catalog[DW2_ITEM_TITANIUM_SHARD].weight > dw2_catalog[DW2_ITEM_TITANIUM_BEAM].weight);
    CHECK(4 * dw2_catalog[DW2_ITEM_ORE_CHIP].cost > dw2_catalog[DW2_ITEM_AETHER_ORE].cost);
    CHECK(4 * dw2_catalog[DW2_ITEM_ORE_CHIP].weight > dw2_catalog[DW2_ITEM_AETHER_ORE].weight);
}

/* ---- the circuit (ruleset 1's rules, unchanged) -------------------------------------------------------------------------------------- */
static void test_straight_chain_fires(void) {
    /* Generator -> Conductor -> Railgun into an empty lane: the slug reaches the hull for 30 x DW2_HULL_FACTOR. Threshold 30, +10 a tick: the 3rd tick. */
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0);
    dw2_ship_place(&me, DW2_ITEM_CONDUCTOR, 0, 1, 0);
    dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 2); CHECK(foe.hull_pct == 100.0f);
    ticks(&me, &foe, 1); NEAR(foe.hull_pct, 100.0f - 30.0f * DW2_HULL_FACTOR);
}

static void test_splitter_one_to_many(void) {
    Dw2Ship s; dw2_ship_init(&s);
    dw2_ship_place(&s, DW2_ITEM_GENERATOR, 1, 0, 0);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 1, 1, 0);
    int wn = dw2_ship_place(&s, DW2_ITEM_RAILGUN, 0, 1, 3);
    int ws = dw2_ship_place(&s, DW2_ITEM_RAILGUN, 2, 1, 1);
    CHECK(wn >= 0 && ws >= 0);
    Dw2Ship t; foe_ship(&t); dw2_ship_start_combat(&t);
    dw2_ship_start_combat(&s);
    ticks(&s, &t, 1);
    CHECK(s.weapon_charge[wn] == DW2_GENERATOR_OUTPUT / 2.0f);   /* a Splitter DIVIDES power: 5 W each way */
    CHECK(s.weapon_charge[ws] == DW2_GENERATOR_OUTPUT / 2.0f);
}

static void test_many_to_one_adds(void) {
    Dw2Ship s; dw2_ship_init(&s);
    dw2_ship_place(&s, DW2_ITEM_GENERATOR, 1, 0, 0);
    dw2_ship_place(&s, DW2_ITEM_GENERATOR, 0, 1, 1);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 1, 1, 0);
    int weapon = dw2_ship_place(&s, DW2_ITEM_RAILGUN, 2, 1, 1);
    CHECK(weapon >= 0);
    Dw2Ship t; foe_ship(&t); dw2_ship_start_combat(&t);
    dw2_ship_start_combat(&s);
    ticks(&s, &t, 1);
    CHECK(s.weapon_charge[weapon] == DW2_GENERATOR_OUTPUT);       /* two feeds ADD: 5 W + 5 W */
}

static void test_back_emf_shatters_loop(void) {
    Dw2Ship s; dw2_ship_init(&s);
    dw2_ship_place(&s, DW2_ITEM_GENERATOR, 1, 0, 0);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 1, 1, 3);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 1, 2, 0);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 2, 2, 1);
    dw2_ship_place(&s, DW2_ITEM_SPLITTER, 2, 1, 2);
    dw2_ship_start_combat(&s);
    Dw2Ship t; foe_ship(&t); dw2_ship_start_combat(&t);
    for (int k = 1; k <= 3; k++) { ticks(&s, &t, 1); CHECK(s.shatter_events == 0); }
    Dw2EventLog log; memset(&log, 0, sizeof log);
    dw2_ship_tick(&s, &t, &log);
    CHECK(s.shatter_events == 1);
    dw2_mask ring = dw2_bit(dw2_cell(1, 1)) | dw2_bit(dw2_cell(1, 2)) | dw2_bit(dw2_cell(2, 2)) | dw2_bit(dw2_cell(2, 1));
    CHECK((s.burned & ring) == ring && (s.dead & ring) == ring);
    for (int i = 0; i < DW2_GRID_CELLS; i++) if (ring & dw2_bit(i)) CHECK(s.cell_hp[i] == 0.0f);
    int burns = 0; for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_BURN) burns++;
    CHECK(burns == 4);
    ticks(&s, &t, 1);
    CHECK(s.shatter_events == 1);
}

static void test_match_result_timeout_tiebreak(void) {
    Dw2Ship a, b;
    dw2_ship_init(&a); dw2_ship_place(&a, DW2_ITEM_BULWARK, 0, 0, 0); dw2_ship_start_combat(&a);
    dw2_ship_init(&b); b.idx = 1; dw2_ship_place(&b, DW2_ITEM_BULWARK, 0, 0, 0); dw2_ship_start_combat(&b);
    CHECK(dw2_match_result(&a, &b, 0) == DW2_RESULT_ONGOING);
    a.hull_pct = 60.0f; b.hull_pct = 40.0f;
    CHECK(dw2_match_result(&a, &b, DW2_MATCH_TIMEOUT_TICKS) == DW2_RESULT_SELF_WIN);
    CHECK(dw2_match_result(&b, &a, DW2_MATCH_TIMEOUT_TICKS) == DW2_RESULT_ENEMY_WIN);
    /* equal hull: the ship with more of its structure still standing */
    a.hull_pct = b.hull_pct = 50.0f;
    b.cell_hp[0] = 10.0f;
    CHECK(dw2_match_result(&a, &b, DW2_MATCH_TIMEOUT_TICKS) == DW2_RESULT_SELF_WIN);
    /* equal structure: the more expensive ship */
    b.cell_hp[0] = b.cell_max[0];
    a.cargo_value = 500; b.cargo_value = 300;
    CHECK(dw2_match_result(&a, &b, DW2_MATCH_TIMEOUT_TICKS) == DW2_RESULT_SELF_WIN);
    a.hull_pct = 0.0f; b.hull_pct = 0.0f;
    CHECK(dw2_match_result(&a, &b, DW2_MATCH_TIMEOUT_TICKS) == DW2_RESULT_TIE);
}

/* ---- the fight ------------------------------------------------------------------------------------------------------------------------ */
/* A Generator at (r,0) feeding a weapon at (r,1) through its west port. */
static int armed(Dw2Ship *s, int row, int weapon) {
    dw2_ship_place(s, DW2_ITEM_GENERATOR, row, 0, 0);
    return dw2_ship_place(s, weapon, row, 1, 0);
}

static void test_laser_needs_a_clear_lane(void) {
    /* A laser at (1,1) with a Bulwark in front of it at (0,1) holds fire (and holds its charge, ready); destroy the Bulwark and it fires. */
    Dw2Ship me; dw2_ship_init(&me);
    int w = armed(&me, 1, DW2_ITEM_PULSE_LASER);
    dw2_ship_place(&me, DW2_ITEM_BULWARK, 0, 1, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 4);
    CHECK(foe.hull_pct == 100.0f);
    CHECK(me.weapon_charge[w] == 10.0f);        /* held at its threshold, not piling up */
    me.cell_hp[dw2_cell(0, 1)] = 0.0f; me.dead |= dw2_bit(dw2_cell(0, 1));
    ticks(&me, &foe, 1);
    NEAR(foe.hull_pct, 100.0f - 2.0f * LASER * DW2_HULL_FACTOR);    /* the held shot and this second's: two */
    /* the same laser on the bow row is never blocked */
    Dw2Ship m2; dw2_ship_init(&m2); armed(&m2, 0, DW2_ITEM_PULSE_LASER); dw2_ship_start_combat(&m2);
    CHECK(dw2_lane_clear_ahead(&m2, dw2_cell(0, 1)) == 1);
    /* a railgun fires through its own ship */
    Dw2Ship m3; dw2_ship_init(&m3); armed(&m3, 1, DW2_ITEM_RAILGUN); dw2_ship_place(&m3, DW2_ITEM_BULWARK, 0, 1, 0); dw2_ship_start_combat(&m3);
    Dw2Ship f3; foe_ship(&f3); dw2_ship_start_combat(&f3);
    ticks(&m3, &f3, 3);
    NEAR(f3.hull_pct, 100.0f - 30.0f * DW2_HULL_FACTOR);
}

static void test_laser_hits_the_first_part_in_its_lane(void) {
    Dw2Ship me; dw2_ship_init(&me); armed(&me, 0, DW2_ITEM_PULSE_LASER); dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe);
    dw2_ship_place(&foe, DW2_ITEM_CONDUCTOR, 2, 1, 0);      /* lane 1: first part is the Conductor at row 2 ... */
    dw2_ship_place(&foe, DW2_ITEM_BULWARK, 4, 1, 0);        /* ... then a Bulwark behind it */
    dw2_ship_start_combat(&foe);
    Dw2EventLog log; memset(&log, 0, sizeof log);
    dw2_ship_tick(&me, &foe, &log);
    NEAR(hp(&foe, 2, 1), 10.0f - LASER);                                        /* 7 on the Conductor (10 HP) */
    CHECK(hp(&foe, 4, 1) == 45.0f);                                             /* a laser does not go through */
    CHECK(log.ev[0].type == DW2_EV_FIRE && log.ev[0].side == 0 && log.ev[0].c == 1);
    CHECK(log.ev[1].type == DW2_EV_HIT && log.ev[1].side == 1 && log.ev[1].a == dw2_cell(2, 1) && !(log.ev[1].d & DW2_HITF_DESTROYED));
    memset(&log, 0, sizeof log);
    dw2_ship_tick(&me, &foe, &log);                                             /* the second shot destroys it */
    CHECK(hp(&foe, 2, 1) == 0.0f && (foe.dead & dw2_bit(dw2_cell(2, 1))));
    CHECK(log.ev[1].type == DW2_EV_HIT && (log.ev[1].d & DW2_HITF_DESTROYED));
    dw2_ship_tick(&me, &foe, NULL);                                             /* now the Bulwark: lasers do half to it */
    NEAR(hp(&foe, 4, 1), 45.0f - LASER * 0.5f);
    CHECK(foe.hull_pct == 100.0f);
}

static void test_rail_pierces_and_carries(void) {
    Dw2Ship me; dw2_ship_init(&me); armed(&me, 0, DW2_ITEM_RAILGUN); dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe);
    dw2_ship_place(&foe, DW2_ITEM_CONDUCTOR, 1, 1, 0);      /* 10 HP */
    dw2_ship_place(&foe, DW2_ITEM_BULWARK, 3, 1, 0);        /* 45 HP */
    dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 3);
    /* 30 on the Conductor (10 HP): destroyed, 20 left x0.8 = 16 carries to the Bulwark */
    CHECK(hp(&foe, 1, 1) == 0.0f);
    NEAR(hp(&foe, 3, 1), 45.0f - 16.0f);
    CHECK(foe.hull_pct == 100.0f);
    /* a slug that destroys everything in its lane reaches the hull */
    Dw2Ship f2; foe_ship(&f2); dw2_ship_place(&f2, DW2_ITEM_CONDUCTOR, 1, 1, 0); dw2_ship_start_combat(&f2);
    Dw2Ship m2; dw2_ship_init(&m2); armed(&m2, 0, DW2_ITEM_RAILGUN); dw2_ship_start_combat(&m2);
    ticks(&m2, &f2, 3);
    NEAR(f2.hull_pct, 100.0f - (30.0f - 10.0f) * DW2_RAIL_CARRY * DW2_HULL_FACTOR);
}

static void test_cannon_lands_on_the_deepest_part_and_splashes(void) {
    Dw2Ship me; dw2_ship_init(&me); int w = armed(&me, 0, DW2_ITEM_SIEGE_CANNON);   /* weapon cell (0,1): lane 1 */
    CHECK(w >= 0); dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe);
    dw2_ship_place(&foe, DW2_ITEM_BULWARK, 0, 1, 0);        /* in front: the shell goes over it */
    dw2_ship_place(&foe, DW2_ITEM_GENERATOR, 5, 1, 0);      /* deepest in lane 1: 20 HP */
    dw2_ship_place(&foe, DW2_ITEM_CONDUCTOR, 5, 2, 0);      /* touches it: splash 22.5 on 10 HP */
    dw2_ship_place(&foe, DW2_ITEM_BULWARK, 4, 1, 0);        /* touches it from the bow side: splash 22.5 on 45 HP */
    dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 3);                                    /* 30 charge at 10 W: the 3rd second */
    CHECK(hp(&foe, 0, 1) == 45.0f);
    CHECK(hp(&foe, 5, 1) == 0.0f);                          /* 45 on 20 HP */
    CHECK(hp(&foe, 5, 2) == 0.0f);                          /* splash 22.5 on 10 HP */
    NEAR(hp(&foe, 4, 1), 45.0f - 45.0f * DW2_CANNON_SPLASH);
    CHECK(foe.hull_pct == 100.0f);
    /* a cannon with its barrel shot off is dead */
    Dw2Ship m2; dw2_ship_init(&m2); armed(&m2, 0, DW2_ITEM_SIEGE_CANNON); dw2_ship_start_combat(&m2);
    m2.cell_hp[dw2_cell(0, 2)] = 0.0f; m2.dead |= dw2_bit(dw2_cell(0, 2));
    Dw2Ship f2; foe_ship(&f2); dw2_ship_start_combat(&f2);
    ticks(&m2, &f2, 6);
    CHECK(f2.hull_pct == 100.0f);
}

static void test_materials_resist_the_right_weapons(void) {
    CHECK(dw2_catalog[DW2_ITEM_BULWARK].x_laser == 0.5f && dw2_catalog[DW2_ITEM_BULWARK].x_rail == 1.0f);
    CHECK(dw2_catalog[DW2_ITEM_TITANIUM_BEAM].x_rail == 0.5f && dw2_catalog[DW2_ITEM_TITANIUM_BEAM].x_laser == 1.0f);
    CHECK(dw2_catalog[DW2_ITEM_AETHER_ORE].x_laser == 0.2f && dw2_catalog[DW2_ITEM_AETHER_ORE].x_cannon > 1.0f);
    /* a rail slug (30) on Titanium (20 HP a cell, x0.5) does 15 and the cell stands; on Aether Ore (15 HP) it destroys it */
    Dw2Ship me; dw2_ship_init(&me); armed(&me, 0, DW2_ITEM_RAILGUN); dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_place(&foe, DW2_ITEM_TITANIUM_BEAM, 2, 0, 0); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 3);
    NEAR(hp(&foe, 2, 1), 20.0f - 15.0f);
    Dw2Ship m2; dw2_ship_init(&m2); armed(&m2, 0, DW2_ITEM_PULSE_LASER); dw2_ship_start_combat(&m2);
    Dw2Ship f2; foe_ship(&f2); dw2_ship_place(&f2, DW2_ITEM_AETHER_ORE, 2, 0, 0); dw2_ship_start_combat(&f2);
    ticks(&m2, &f2, 1);
    NEAR(hp(&f2, 2, 1), 15.0f - LASER * 0.2f);
}

static void test_shield_soaks_first(void) {
    Dw2Ship me; dw2_ship_init(&me); armed(&me, 0, DW2_ITEM_RAILGUN); dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    foe.shield = 20.0f;
    Dw2EventLog log; memset(&log, 0, sizeof log);
    for (int t = 0; t < 3; t++) dw2_ship_tick(&me, &foe, &log);
    NEAR(foe.shield, 0.0f);
    NEAR(foe.hull_pct, 100.0f - 10.0f * DW2_HULL_FACTOR);        /* 30 - 20 absorbed = 10 reaches the hull */
    int sh = 0; for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_SHIELD) { sh++; CHECK(log.ev[i].a == 20); }
    CHECK(sh == 1);
}

static void test_light_ships_dodge_slugs_but_not_beams(void) {
    /* weight 4 of 40: dodge chance (40-4) x 1.5 = capped at 50% -> every second slug misses */
    Dw2Ship foe; foe_ship(&foe); dw2_ship_place(&foe, DW2_ITEM_CONDUCTOR, 5, 5, 0); dw2_ship_start_combat(&foe);
    CHECK(foe.evade_pct == DW2_EVADE_MAX);
    Dw2Ship me; dw2_ship_init(&me); armed(&me, 0, DW2_ITEM_RAILGUN); dw2_ship_start_combat(&me);
    int dodged = 0, hits = 0;
    Dw2EventLog log;
    for (int t = 0; t < 12; t++) {
        memset(&log, 0, sizeof log); dw2_ship_tick(&me, &foe, &log);
        for (int i = 0; i < log.n; i++) { if (log.ev[i].type == DW2_EV_EVADE) dodged++; if (log.ev[i].type == DW2_EV_HULL) hits++; }
    }
    CHECK(dodged == 2 && hits == 2);                              /* 4 slugs in 12 s: 50% -> the 2nd and 4th are dodged */
    /* a laser cannot be dodged */
    Dw2Ship m2; dw2_ship_init(&m2); armed(&m2, 0, DW2_ITEM_PULSE_LASER); dw2_ship_start_combat(&m2);
    Dw2Ship f2; foe_ship(&f2); dw2_ship_start_combat(&f2);
    ticks(&m2, &f2, 6);
    NEAR(f2.hull_pct, 100.0f - 6 * LASER * DW2_HULL_FACTOR);
    /* a heavy ship (40 t) never dodges */
    Dw2Ship heavy; foe_ship(&heavy);
    for (int c = 0; c < 6; c++) dw2_ship_place(&heavy, DW2_ITEM_BULWARK, 5, c, 0);
    for (int c = 0; c < 2; c++) dw2_ship_place(&heavy, DW2_ITEM_BULWARK, 4, c, 0);
    dw2_ship_start_combat(&heavy);
    CHECK(heavy.weight == 40 && heavy.evade_pct == 0.0f);
}

static void test_quantum_core_heat_and_cryo_vents(void) {
    /* A Core at (0,0)-(1,1) cooks the Conductor at (0,2) for 2 a second (10 HP: dead in 5) ... */
    Dw2Ship hot; dw2_ship_init(&hot);
    dw2_ship_place(&hot, DW2_ITEM_QUANTUM_CORE, 0, 0, 0);
    dw2_ship_place(&hot, DW2_ITEM_CONDUCTOR, 0, 2, 0);
    dw2_ship_start_combat(&hot);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    CHECK(dw2_cell_heat(&hot, dw2_cell(0, 2)) == 2 && dw2_cell_cooled(&hot, dw2_cell(0, 2)) == 0);
    ticks(&hot, &foe, 4); NEAR(hp(&hot, 0, 2), 2.0f);
    Dw2EventLog log; memset(&log, 0, sizeof log); dw2_ship_tick(&hot, &foe, &log);
    CHECK(hp(&hot, 0, 2) == 0.0f && (hot.dead & dw2_bit(dw2_cell(0, 2))));
    int heat = 0; for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_HEAT && log.ev[i].d == 1) heat++;
    CHECK(heat == 1);
    /* ... unless a Cryo-Vent touches it */
    Dw2Ship cool; dw2_ship_init(&cool);
    dw2_ship_place(&cool, DW2_ITEM_QUANTUM_CORE, 0, 0, 0);
    dw2_ship_place(&cool, DW2_ITEM_CONDUCTOR, 0, 2, 0);
    dw2_ship_place(&cool, DW2_ITEM_CRYO_VENT, 0, 3, 0);            /* (0,3),(0,4): touches the Conductor */
    dw2_ship_start_combat(&cool);
    CHECK(dw2_cell_cooled(&cool, dw2_cell(0, 2)) == 1 && dw2_cell_heat(&cool, dw2_cell(0, 2)) == 0);
    ticks(&cool, &foe, 10); CHECK(hp(&cool, 0, 2) == 10.0f);
    /* a Vent itself is immune */
    Dw2Ship two; dw2_ship_init(&two);
    dw2_ship_place(&two, DW2_ITEM_QUANTUM_CORE, 0, 0, 0);
    dw2_ship_place(&two, DW2_ITEM_CRYO_VENT, 0, 2, 0);
    dw2_ship_start_combat(&two);
    ticks(&two, &foe, 5); CHECK(hp(&two, 0, 2) == 20.0f);
    /* 40 W for the space of four Generators: every Core cell is a source, splitting its 10 W between its two outer sides */
    Dw2Ship pw; dw2_ship_init(&pw);
    dw2_ship_place(&pw, DW2_ITEM_QUANTUM_CORE, 2, 0, 0);
    dw2_ship_place(&pw, DW2_ITEM_PULSE_LASER, 2, 2, 0);
    dw2_ship_place(&pw, DW2_ITEM_PULSE_LASER, 3, 2, 0);
    dw2_ship_start_combat(&pw); ticks(&pw, &foe, 1);
    CHECK(pw.weapon_charge[1] == 5.0f && pw.weapon_charge[2] == 5.0f);
}

static void test_biomass_grows_back_what_it_touches(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_BIOMASS, 0, 0, 0);          /* cells (0,0),(1,0),(2,0),(2,1): a bent colony */
    dw2_ship_place(&me, DW2_ITEM_BULWARK, 0, 1, 0);          /* touches the colony's (0,0) and nothing else of it */
    dw2_ship_start_combat(&me);
    int plate = dw2_cell(0, 1);
    me.cell_hp[plate] = 10.0f;
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    dw2_ship_tick(&me, &foe, NULL);
    NEAR(me.cell_hp[plate], 10.0f + DW2_BIOMASS_HEAL_OTHER);
    /* the colony grows back itself, 1 HP per touching cell of its own: (1,0) touches (0,0) and (2,0) */
    me.cell_hp[dw2_cell(1, 0)] = 5.0f;
    dw2_ship_tick(&me, &foe, NULL);
    NEAR(me.cell_hp[dw2_cell(1, 0)], 5.0f + 2.0f * DW2_BIOMASS_HEAL_OWN);
    me.cell_hp[plate] = 44.5f; dw2_ship_tick(&me, &foe, NULL);
    CHECK(me.cell_hp[plate] == me.cell_max[plate]);              /* never past full */
    me.cell_hp[plate] = 0.0f; me.dead |= dw2_bit(plate);
    dw2_ship_tick(&me, &foe, NULL);
    CHECK(me.cell_hp[plate] == 0.0f);                            /* a destroyed part is not brought back */
}

static void test_aegis_repairs_the_weakest_part_nearby(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 2, 0, 0);
    dw2_ship_place(&me, DW2_ITEM_AEGIS, 2, 1, 0);            /* its 3x3 is rows 1-3, cols 0-2 */
    dw2_ship_place(&me, DW2_ITEM_BULWARK, 1, 2, 0);          /* in the 3x3 */
    dw2_ship_place(&me, DW2_ITEM_BULWARK, 5, 5, 0);          /* out of it */
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    me.cell_hp[dw2_cell(1, 2)] = 20.0f; me.cell_hp[dw2_cell(5, 5)] = 5.0f;
    Dw2EventLog log; memset(&log, 0, sizeof log); dw2_ship_tick(&me, &foe, &log);
    NEAR(me.cell_hp[dw2_cell(1, 2)], 20.0f + DW2_AEGIS_REPAIR);       /* 10 W fed: 4 HP */
    CHECK(me.cell_hp[dw2_cell(5, 5)] == 5.0f);
    int rep = 0; for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_REPAIR && log.ev[i].d == dw2_cell(2, 1)) rep++;
    CHECK(rep == 1);
    /* it picks the part closest to breaking by FRACTION: the Generator at 5/20 over the Bulwark at 24/45 */
    me.cell_hp[dw2_cell(2, 0)] = 5.0f; dw2_ship_tick(&me, &foe, NULL);
    NEAR(me.cell_hp[dw2_cell(2, 0)], 5.0f + DW2_AEGIS_REPAIR);
    /* an unfed Aegis does nothing */
    Dw2Ship m2; dw2_ship_init(&m2); dw2_ship_place(&m2, DW2_ITEM_AEGIS, 3, 3, 0); dw2_ship_place(&m2, DW2_ITEM_BULWARK, 3, 4, 0);
    dw2_ship_start_combat(&m2); m2.cell_hp[dw2_cell(3, 4)] = 10.0f; dw2_ship_tick(&m2, &foe, NULL);
    CHECK(m2.cell_hp[dw2_cell(3, 4)] == 10.0f);
}

static void test_aether_ore_boosts_a_touching_aegis(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 2, 0, 0);
    dw2_ship_place(&me, DW2_ITEM_AEGIS, 2, 1, 0);
    dw2_ship_place(&me, DW2_ITEM_AETHER_ORE, 3, 1, 0);       /* (3,1),(3,2),(4,1),(4,2): touches the Aegis from below */
    dw2_ship_start_combat(&me);
    CHECK(dw2_aegis_boost(&me, dw2_cell(2, 1)) == 1.5f);
    me.cell_hp[dw2_cell(3, 2)] = 3.0f;
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    dw2_ship_tick(&me, &foe, NULL);
    NEAR(me.cell_hp[dw2_cell(3, 2)], 3.0f + DW2_AEGIS_REPAIR * 1.5f);
}

static void test_mender_heals_and_caps(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, DW2_ITEM_MENDER, 0, 1, 0);
    dw2_ship_start_combat(&me);
    me.hull_pct = 50.0f;
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 3);
    NEAR(me.hull_pct, 56.0f);
    me.hull_pct = 99.0f; ticks(&me, &foe, 1);
    CHECK(me.hull_pct == 100.0f);
    Dw2Ship m2; dw2_ship_init(&m2); dw2_ship_place(&m2, DW2_ITEM_MENDER, 3, 3, 0); dw2_ship_start_combat(&m2);
    m2.hull_pct = 50.0f; ticks(&m2, &foe, 1);
    CHECK(m2.hull_pct == 50.0f);
}

static void test_a_destroyed_part_cuts_the_circuit(void) {
    /* Generator -> Conductor -> Conductor -> Railgun: shoot the middle wire and the gun stops charging */
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0);
    dw2_ship_place(&me, DW2_ITEM_CONDUCTOR, 0, 1, 0);
    dw2_ship_place(&me, DW2_ITEM_CONDUCTOR, 0, 2, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 3, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 10.0f);
    me.cell_hp[dw2_cell(0, 2)] = 0.0f; me.dead |= dw2_bit(dw2_cell(0, 2));
    ticks(&me, &foe, 5); CHECK(me.weapon_charge[w] == 10.0f);       /* nothing more arrives */
}

static void test_titanium_boosts_touching_weapons(void) {
    Dw2Ship me; dw2_ship_init(&me); int w = armed(&me, 0, DW2_ITEM_PULSE_LASER);
    dw2_ship_start_combat(&me);
    CHECK(dw2_weapon_boost(&me, w) == 1.0f);
    Dw2Ship m2; dw2_ship_init(&m2); w = armed(&m2, 0, DW2_ITEM_PULSE_LASER);
    dw2_ship_place(&m2, DW2_ITEM_TITANIUM_BEAM, 1, 0, 0);        /* touches both the generator and the laser from below */
    dw2_ship_start_combat(&m2);
    CHECK(dw2_weapon_boost(&m2, w) == 1.5f);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&m2, &foe, 1);
    NEAR(foe.hull_pct, 100.0f - LASER * 1.5f * DW2_HULL_FACTOR);
    /* split into Shards and the boost is x1.25 (still there, smaller), a destroyed Beam cell boosts nothing */
    Dw2Ship m3; dw2_ship_init(&m3); w = armed(&m3, 0, DW2_ITEM_PULSE_LASER);
    dw2_ship_place(&m3, DW2_ITEM_TITANIUM_SHARD, 1, 1, 0);
    dw2_ship_start_combat(&m3);
    CHECK(dw2_weapon_boost(&m3, w) == 1.25f);
    m3.cell_hp[dw2_cell(1, 1)] = 0.0f; m3.dead |= dw2_bit(dw2_cell(1, 1));
    CHECK(dw2_weapon_boost(&m3, w) == 1.0f);
}

static void test_events_are_a_true_account(void) {
    /* every HIT/HULL event's numbers add up to what really happened to the target */
    Dw2Ship a; dw2_ship_init(&a); armed(&a, 0, DW2_ITEM_RAILGUN); dw2_ship_place(&a, DW2_ITEM_BULWARK, 2, 1, 0); dw2_ship_start_combat(&a);
    Dw2Ship b; dw2_ship_init(&b); b.idx = 1; armed(&b, 0, DW2_ITEM_RAILGUN); dw2_ship_place(&b, DW2_ITEM_BULWARK, 2, 1, 0); dw2_ship_start_combat(&b);
    float sa = dw2_ship_structure(&a), sb = dw2_ship_structure(&b);
    float lost_a = 0, lost_b = 0;
    for (int t = 0; t < 12; t++) {
        Dw2EventLog log; memset(&log, 0, sizeof log);
        dw2_match_tick(&a, &b, t, &log);
        for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_HIT) { if (log.ev[i].side == 0) lost_a += log.ev[i].b; else lost_b += log.ev[i].b; }
    }
    NEAR(lost_a, sa - dw2_ship_structure(&a));
    NEAR(lost_b, sb - dw2_ship_structure(&b));
    CHECK(lost_a > 0 && lost_b > 0);
}

static void test_simultaneous_volleys(void) {
    /* both rails fire in second 3, and each kills the other's rail: a gun destroyed this second still fired this second */
    Dw2Ship a, b; dw2_ship_init(&a); dw2_ship_init(&b); b.idx = 1;
    int wa = armed(&a, 0, DW2_ITEM_PULSE_LASER), wb = armed(&b, 0, DW2_ITEM_PULSE_LASER);
    dw2_ship_start_combat(&a); dw2_ship_start_combat(&b);
    for (int t = 0; t < 5; t++) dw2_match_tick(&a, &b, t, NULL);
    CHECK(wa == wb);
    /* 35 HP lasers, 7 a shot: 5 volleys each, both at 0 -- 5 x 7 = 35 >= 35 -> both die in the 5th second together */
    CHECK(a.cell_hp[dw2_cell(0, 1)] == 0.0f && b.cell_hp[dw2_cell(0, 1)] == 0.0f);
    CHECK(a.hull_pct == b.hull_pct);
}

static void test_dummy_ship_fuzz(void) {
    /* no crash, no ASan/UBSan complaint, and the invariants hold, across many random legal ships fighting each other */
    for (int seed = 0; seed < 400; seed++) {
        srand((unsigned)seed);
        Dw2Ship a, b; dw2_ship_init(&a); dw2_ship_init(&b); b.idx = 1;
        for (int i = 0; i < 40; i++) {
            dw2_ship_place(&a, rand() % DW2_ITEM_COUNT, rand() % DW2_GRID_H, rand() % DW2_GRID_W, rand() % 4);
            dw2_ship_place(&b, rand() % DW2_ITEM_COUNT, rand() % DW2_GRID_H, rand() % DW2_GRID_W, rand() % 4);
        }
        CHECK(a.cost <= DW2_BUDGET_CREDITS && a.weight <= DW2_BUDGET_WEIGHT && b.cost <= DW2_BUDGET_CREDITS && b.weight <= DW2_BUDGET_WEIGHT);
        dw2_ship_start_combat(&a); dw2_ship_start_combat(&b);
        for (int t = 0; t < DW2_MATCH_TIMEOUT_TICKS; t++) {
            Dw2EventLog log; memset(&log, 0, sizeof log);
            dw2_match_tick(&a, &b, t, &log);
            Dw2Ship *s2[2] = { &a, &b };
            for (int k = 0; k < 2; k++) {
                CHECK(s2[k]->hull_pct >= 0.0f && s2[k]->hull_pct <= 100.0f);
                for (int c = 0; c < DW2_GRID_CELLS; c++) {
                    CHECK(s2[k]->cell_hp[c] >= 0.0f && s2[k]->cell_hp[c] <= s2[k]->cell_max[c]);
                    CHECK(!(s2[k]->dead & dw2_bit(c)) || s2[k]->cell_hp[c] == 0.0f);
                }
            }
            if (dw2_match_result(&a, &b, t + 1) != DW2_RESULT_ONGOING) break;
        }
    }
    CHECK(1);
}

/* ---- Round-break mini-game (core/round.h): pure-logic tests ---- */
static void test_round_target_and_grading_windows(void) {
    uint16_t target = dw2_round_target_ms(12345u, 3);
    CHECK(target >= DW2_ROUND_TARGET_MIN_MS && target < DW2_ROUND_TARGET_MIN_MS + DW2_ROUND_TARGET_MS_SPAN);
    CHECK(dw2_round_target_ms(12345u, 3) == target);
    int grade;
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, target, 0, &grade); CHECK(grade == DW2_GRADE_PERFECT);
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, target - DW2_ROUND_PERFECT_MS, 0, &grade); CHECK(grade == DW2_GRADE_PERFECT);
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, (uint32_t)target + DW2_ROUND_PERFECT_MS, 0, &grade); CHECK(grade == DW2_GRADE_PERFECT);
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, (uint32_t)target + DW2_ROUND_PERFECT_MS + 1, 0, &grade); CHECK(grade == DW2_GRADE_GOOD);
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS, 0, &grade); CHECK(grade == DW2_GRADE_GOOD);
    dw2_round_grade(12345u, 3, DW2_CALL_BRACE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS + 1, 0, &grade); CHECK(grade == DW2_GRADE_MISS);
}

static void test_round_overcharge_payoffs(void) {
    int grade; Dw2RoundEffect e;
    uint16_t target = dw2_round_target_ms(777u, 1);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, target, 0, &grade);
    CHECK(grade == DW2_GRADE_PERFECT); CHECK(e.dmg_mult == 1.5f); CHECK(e.self_damage == 0.0f); CHECK(e.armor_bonus == 0.0f);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, target, 1, &grade);
    CHECK(e.dmg_mult == 1.0f + 0.5f * 1.6f);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, target, 2, &grade);
    CHECK(e.dmg_mult == 1.0f + 0.5f * 0.4f);
    uint32_t good_ms = (uint32_t)target + DW2_ROUND_PERFECT_MS + 1;
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, good_ms, 0, &grade);
    CHECK(grade == DW2_GRADE_GOOD); CHECK(e.dmg_mult == 1.25f);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, good_ms, 1, &grade);
    CHECK(e.dmg_mult == 1.0f + 0.25f * 1.6f);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS + 1, 0, &grade);
    CHECK(grade == DW2_GRADE_MISS); CHECK(e.dmg_mult == 1.0f); CHECK(e.self_damage == DW2_ROUND_OVERCHARGE_BACKFIRE_HULL);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS + 1, 2, &grade);
    CHECK(e.self_damage == DW2_ROUND_OVERCHARGE_BACKFIRE_HULL * 1.6f);
    e = dw2_round_grade(777u, 1, DW2_CALL_OVERCHARGE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS + 1, 1, &grade);
    CHECK(e.self_damage == DW2_ROUND_OVERCHARGE_BACKFIRE_HULL * 1.4f);
}

static void test_round_brace_payoffs(void) {
    int grade; Dw2RoundEffect e;
    uint16_t target = dw2_round_target_ms(888u, 2);
    e = dw2_round_grade(888u, 2, DW2_CALL_BRACE, 0, target, 0, &grade);
    CHECK(grade == DW2_GRADE_PERFECT); CHECK(e.armor_bonus == 25.0f); CHECK(e.dmg_mult == 1.0f); CHECK(e.self_damage == 0.0f);
    e = dw2_round_grade(888u, 2, DW2_CALL_BRACE, 0, target, 1, &grade); CHECK(e.armor_bonus == 25.0f * 1.6f);
    e = dw2_round_grade(888u, 2, DW2_CALL_BRACE, 0, target, 2, &grade); CHECK(e.armor_bonus == 25.0f * 0.4f);
    CHECK(dw2_standing(50, 0, 40, 20) == DW2_STAND_BEHIND); CHECK(dw2_standing(50, 20, 40, 20) == DW2_STAND_AHEAD); CHECK(dw2_standing(40, 20, 50, 10) == DW2_STAND_LEVEL);
    e = dw2_round_grade(888u, 2, DW2_CALL_BRACE, 0, (uint32_t)target + DW2_ROUND_GOOD_MS + 1, 0, &grade);
    CHECK(grade == DW2_GRADE_MISS); CHECK(e.armor_bonus == 0.0f); CHECK(e.self_damage == 0.0f);
}

static void test_round_no_call_is_neutral(void) {
    int grade;
    Dw2RoundEffect e = dw2_round_grade(1u, 1, DW2_CALL_OVERCHARGE, 1, 999999u, 1, &grade);
    CHECK(grade == DW2_GRADE_NONE); CHECK(e.dmg_mult == 1.0f); CHECK(e.armor_bonus == 0.0f); CHECK(e.self_damage == 0.0f);
}

static void test_round_apply_effect(void) {
    Dw2Ship s; dw2_ship_init(&s); dw2_ship_start_combat(&s);
    CHECK(s.dmg_mult == 1.0f);
    float shield_before = s.shield;
    Dw2RoundEffect e_brace = { 1.0f, 20.0f, 0.0f };
    dw2_ship_apply_round_effect(&s, e_brace);
    CHECK(s.shield == shield_before + 20.0f); CHECK(s.dmg_mult == 1.0f);
    Dw2RoundEffect e_overcharge_perfect = { 1.5f, 0.0f, 0.0f };
    dw2_ship_apply_round_effect(&s, e_overcharge_perfect);
    CHECK(s.dmg_mult == 1.5f);
    s.hull_pct = 5.0f;
    Dw2RoundEffect e_backfire = { 1.0f, 0.0f, DW2_ROUND_OVERCHARGE_BACKFIRE_HULL };
    dw2_ship_apply_round_effect(&s, e_backfire);
    CHECK(s.hull_pct == 0.0f); CHECK(s.dmg_mult == 1.0f);
}

static void test_dmg_mult_scales_damage(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, DW2_ITEM_CONDUCTOR, 0, 1, 0); dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&me);
    me.dmg_mult = 2.0f;
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 2); CHECK(foe.hull_pct == 100.0f);
    ticks(&me, &foe, 1);
    NEAR(foe.hull_pct, 100.0f - 2.0f * 30.0f * DW2_HULL_FACTOR);
}

static void test_cannon_decide_always_fires(void) {
    CHECK(dw2_cannon_decide(0, 0) == DW2_CANNON_FIRE);
    CHECK(dw2_cannon_decide(0, 1) == DW2_CANNON_FIRE);
    CHECK(dw2_cannon_decide(1, 0) == DW2_CANNON_FIRE);
    CHECK(dw2_cannon_decide(1, 1) == DW2_CANNON_FIRE);
}

/* ---- the circuit parts ---- */
static void test_amplifier_lifts_everything_downstream(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, DW2_ITEM_AMPLIFIER, 0, 1, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 15.0f);        /* 10 x 1.5 */
    ticks(&me, &foe, 1); NEAR(foe.hull_pct, 100.0f - 30.0f * DW2_HULL_FACTOR);   /* 30 at second 2 instead of second 3 */
    Dw2Ship m2; dw2_ship_init(&m2);
    dw2_ship_place(&m2, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&m2, DW2_ITEM_AMPLIFIER, 0, 1, 0);
    dw2_ship_place(&m2, DW2_ITEM_AMPLIFIER, 0, 2, 0); dw2_ship_place(&m2, DW2_ITEM_PULSE_LASER, 0, 3, 0);
    dw2_ship_start_combat(&m2);
    Dw2Ship f2; foe_ship(&f2); dw2_ship_start_combat(&f2);
    ticks(&m2, &f2, 1);                                              /* 22.5 charge: two shots of 10, 2.5 left */
    NEAR(f2.hull_pct, 100.0f - 2 * LASER * DW2_HULL_FACTOR);
}

static void test_resistors_cut_power(void) {
    int items[3] = { DW2_ITEM_RESISTOR_1, DW2_ITEM_RESISTOR_2, DW2_ITEM_RESISTOR_4 };
    float want[3] = { 7.5f, 5.0f, 2.5f };
    for (int k = 0; k < 3; k++) {
        Dw2Ship me; dw2_ship_init(&me);
        dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, items[k], 0, 1, 0);
        int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
        Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
        dw2_ship_start_combat(&me); ticks(&me, &foe, 1);
        CHECK(me.weapon_charge[w] == want[k]);
    }
}

static void test_capacitor_bursts_when_full(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, DW2_ITEM_CAPACITOR, 0, 1, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 3);
    CHECK(me.cell_q[1] == 30.0f && me.weapon_charge[w] == 0.0f);
    ticks(&me, &foe, 1);                                              /* second 4: the whole 30 in one tick */
    NEAR(foe.hull_pct, 100.0f - 30.0f * DW2_HULL_FACTOR);
    CHECK(me.cell_q[1] == 10.0f);
    Dw2Ship m2; dw2_ship_init(&m2);
    dw2_ship_place(&m2, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&m2, DW2_ITEM_CAPACITOR, 0, 1, 2);
    dw2_ship_start_combat(&m2); ticks(&m2, &foe, 3);
    CHECK(m2.cell_q[1] == 0.0f);
}

static void test_battery_supplies_without_a_generator(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_BATTERY, 0, 0, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 1, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    CHECK(me.cell_q[0] == DW2_BAT_START);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 15.0f);
    ticks(&me, &foe, 1); NEAR(foe.hull_pct, 100.0f - 30.0f * DW2_HULL_FACTOR);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 15.0f && me.cell_q[0] == 0.0f);
    ticks(&me, &foe, 3); CHECK(me.weapon_charge[w] == 15.0f);
    Dw2Ship m2; dw2_ship_init(&m2);
    dw2_ship_place(&m2, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&m2, DW2_ITEM_BATTERY, 0, 1, 0);
    dw2_ship_start_combat(&m2); ticks(&m2, &foe, 1);
    CHECK(m2.cell_q[1] == 40.0f);
}

static void test_transistor_shut_without_a_gate_feed(void) {
    Dw2Ship shut; dw2_ship_init(&shut);
    dw2_ship_place(&shut, DW2_ITEM_GENERATOR, 1, 0, 0); dw2_ship_place(&shut, DW2_ITEM_TRANSISTOR, 1, 1, 0);
    int ws = dw2_ship_place(&shut, DW2_ITEM_RAILGUN, 1, 2, 0);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    dw2_ship_start_combat(&shut); ticks(&shut, &foe, 5);
    CHECK(shut.weapon_charge[ws] == 0.0f);
}

static void test_transistor_open_after_gate_fed(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 1, 0, 0);
    dw2_ship_place(&me, DW2_ITEM_TRANSISTOR, 1, 1, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 1, 2, 0);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 1, 1);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 0.0f && me.gate_open[dw2_cell(1, 1)] == 1);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == DW2_GENERATOR_OUTPUT);
}

static void test_delayer_holds_two_ticks(void) {
    Dw2Ship me; dw2_ship_init(&me);
    dw2_ship_place(&me, DW2_ITEM_GENERATOR, 0, 0, 0); dw2_ship_place(&me, DW2_ITEM_DELAYER, 0, 1, 0);
    int w = dw2_ship_place(&me, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&me);
    Dw2Ship foe; foe_ship(&foe); dw2_ship_start_combat(&foe);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 0.0f);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 0.0f);
    ticks(&me, &foe, 1); CHECK(me.weapon_charge[w] == 10.0f);
}

/* ---- the wire ---- */
static void test_opp_ship_frames_roundtrip(void) {
    Dw2WireMsg m, d; memset(&m, 0, sizeof m); m.type = DW2_S_OPP_SHIP; m.u.opp_ship.n = 2;
    for (int i = 0; i < DW2_WIRE_MAX_PARTS; i++) m.u.opp_ship.p[i].item = 255;
    m.u.opp_ship.p[0].item = 3; m.u.opp_ship.p[0].row = 1; m.u.opp_ship.p[0].col = 2; m.u.opp_ship.p[0].rot = 3;
    m.u.opp_ship.p[1].item = 23; m.u.opp_ship.p[1].row = 4; m.u.opp_ship.p[1].col = 0; m.u.opp_ship.p[1].rot = 1;
    uint8_t buf[512]; size_t used = 0;
    int n = dw2_encode(&m, buf, sizeof buf);
    CHECK(n == 2 + 1 + 1 + 4 * DW2_WIRE_MAX_PARTS);
    CHECK(dw2_decode(buf, (size_t)n, &d, &used) == 1 && used == (size_t)n);
    CHECK(d.type == DW2_S_OPP_SHIP && d.u.opp_ship.n == 2 && d.u.opp_ship.p[1].item == 23 && d.u.opp_ship.p[35].item == 255);
}

static void test_events_frame_roundtrip(void) {
    Dw2WireMsg m, d; memset(&m, 0, sizeof m); m.type = DW2_S_EVENTS;
    m.u.events.tick = 7; m.u.events.n = 3; m.u.events.dropped = 1; m.u.events.structure_you = 301; m.u.events.structure_opp = 255;
    for (int i = 0; i < 3; i++) { m.u.events.ev[i].type = (uint8_t)(i + 1); m.u.events.ev[i].side = (uint8_t)(i & 1); m.u.events.ev[i].a = (uint8_t)(10 + i); m.u.events.ev[i].e = (uint8_t)(200 + i); }
    uint8_t buf[600]; size_t used = 0;
    int n = dw2_encode(&m, buf, sizeof buf);
    CHECK(n == 2 + 1 + 7 + 3 * 7);
    CHECK(dw2_decode(buf, (size_t)n, &d, &used) == 1 && used == (size_t)n);
    CHECK(d.u.events.tick == 7 && d.u.events.n == 3 && d.u.events.dropped == 1 && d.u.events.structure_you == 301 && d.u.events.structure_opp == 255);
    CHECK(d.u.events.ev[2].type == 3 && d.u.events.ev[2].a == 12 && d.u.events.ev[2].e == 202 && d.u.events.ev[1].side == 1);
    /* a frame whose length disagrees with its count is malformed */
    buf[4] = 5;
    CHECK(dw2_decode(buf, (size_t)n, &d, &used) == -1);
    /* the biggest one fits in a frame */
    memset(&m, 0, sizeof m); m.type = DW2_S_EVENTS; m.u.events.n = DW2_WIRE_MAX_EVENTS;
    n = dw2_encode(&m, buf, sizeof buf);
    CHECK(n > 0 && n - 2 <= DW2_MAX_FRAME_LEN && dw2_decode(buf, (size_t)n, &d, &used) == 1);
}

static void test_match_found_pack_ms_is_wide(void) {
    Dw2WireMsg m, d; memset(&m, 0, sizeof m); m.type = DW2_S_MATCH_FOUND; m.u.match_found.pack_ms = 120000u;
    uint8_t buf[256]; size_t used = 0;
    int n = dw2_encode(&m, buf, sizeof buf);
    CHECK(n == 2 + 1 + 30);
    CHECK(dw2_decode(buf, (size_t)n, &d, &used) == 1 && d.u.match_found.pack_ms == 120000u);
}

int main(void) {
    test_placement_legality();
    test_rotation_math();
    test_budget();
    test_split_pieces();
    test_straight_chain_fires();
    test_splitter_one_to_many();
    test_many_to_one_adds();
    test_back_emf_shatters_loop();
    test_match_result_timeout_tiebreak();
    test_laser_needs_a_clear_lane();
    test_laser_hits_the_first_part_in_its_lane();
    test_rail_pierces_and_carries();
    test_cannon_lands_on_the_deepest_part_and_splashes();
    test_materials_resist_the_right_weapons();
    test_shield_soaks_first();
    test_light_ships_dodge_slugs_but_not_beams();
    test_quantum_core_heat_and_cryo_vents();
    test_biomass_grows_back_what_it_touches();
    test_aegis_repairs_the_weakest_part_nearby();
    test_aether_ore_boosts_a_touching_aegis();
    test_mender_heals_and_caps();
    test_a_destroyed_part_cuts_the_circuit();
    test_titanium_boosts_touching_weapons();
    test_events_are_a_true_account();
    test_simultaneous_volleys();
    test_dummy_ship_fuzz();
    test_round_target_and_grading_windows();
    test_round_overcharge_payoffs();
    test_round_brace_payoffs();
    test_round_no_call_is_neutral();
    test_round_apply_effect();
    test_dmg_mult_scales_damage();
    test_cannon_decide_always_fires();
    test_amplifier_lifts_everything_downstream();
    test_resistors_cut_power();
    test_capacitor_bursts_when_full();
    test_battery_supplies_without_a_generator();
    test_transistor_shut_without_a_gate_feed();
    test_transistor_open_after_gate_fed();
    test_delayer_holds_two_ticks();
    test_opp_ship_frames_roundtrip();
    test_events_frame_roundtrip();
    test_match_found_pack_ms_is_wide();
    printf("test_core_loop: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
