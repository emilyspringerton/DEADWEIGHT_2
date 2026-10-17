/* dw2_ref_dump -- prints what the real combat core does with a set of ships, so another implementation of the self half of the fight
 * (the D2 web client's wasm port, PARENA/stdlib/d2/web/d2_rules.prn) can be checked against it tick by tick.
 *
 * For each ship (the bot pool's, then N random legal layouts, dense wiring, structured circuits): the parts, then what a client needs to know about it before
 * the fight (cost, weight, dodge chance, structure, per-cell heat and cooling, each weapon's boost, each laser's clear lane), then for each of 30 ticks the shots
 * the ship fired, the Back-EMF meltdowns so far, the masks of burned and destroyed cells, and the charge held in stores.
 *
 * The enemy is a ship with an enormous hull and no parts, so every shot reaches it and nothing ends the fight early. To check that the port treats a destroyed
 * part the way the core does (it is not on the board: no power through it, no shot from it, no cover for a laser behind it), the dump also destroys cells on a
 * schedule -- the "K tick cell" lines -- exactly as the server's EVENTS would tell a client a shot had.
 *
 * The second batch is dense wiring (generators, conductors, splitters and weapons in a small corner), which is where closed loops and Back-EMF meltdowns come
 * from; uniformly random ships almost never have one. The third batch is structured circuits: a generator, a chain of random circuit parts along a row, a
 * weapon at the end, sometimes a second generator feeding a transistor's gate from above; these exercise the Capacitor / Transistor / Delayer.
 *
 *   dw2_ref_dump [random-count] [seed] [dense-count] [chain-count] > ref.txt */
#include "../core/combat.h"
#include "../apps/bot/loadouts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

/* splitters make rings; a ring is a closed loop and (after a few ticks) a meltdown */
static const int dense_pool[32] = { 0, 0, 2, 2, 2, 1, 1, 9, 3, 7, 8, 10, 11, 12, 13, 14, 15, 15, 16, 17, 17, 18, 18, 3, 3, 4, 5, 6, 2, 1, 0, 9 };

#define MAXOPS 36
typedef struct { int n; struct { int item, row, col, rot; } op[MAXOPS]; } Ship;

static void dump(int idx, const Ship *lo) {
    Dw2Ship me, foe;
    dw2_ship_init(&me); dw2_ship_init(&foe); foe.idx = 1;
    int placed[MAXOPS][4], pn = 0;
    for (int k = 0; k < lo->n; k++) {
        if (dw2_ship_place(&me, lo->op[k].item, lo->op[k].row, lo->op[k].col, lo->op[k].rot) >= 0) {
            placed[pn][0] = lo->op[k].item; placed[pn][1] = lo->op[k].row; placed[pn][2] = lo->op[k].col; placed[pn][3] = lo->op[k].rot; pn++;
        }
    }
    printf("S %d %d", idx, pn);
    for (int k = 0; k < pn; k++) printf(" %d,%d,%d,%d", placed[k][0], placed[k][1], placed[k][2], placed[k][3]);
    printf("\n");
    dw2_ship_start_combat(&me); dw2_ship_start_combat(&foe);
    foe.hull_pct = 1000000.0f;
    /* what the client can know before the fight */
    printf("A %d %d %d %d", me.cost, me.weight, (int)(dw2_ship_structure_max(&me) + 0.5f), (int)(me.evade_pct * 10.0f + 0.5f));
    unsigned long long cooled = 0;
    for (int c = 0; c < DW2_GRID_CELLS; c++) if (dw2_cell_cooled(&me, c)) cooled |= 1ull << c;
    printf(" %llx", cooled);
    printf(" H");
    for (int c = 0; c < DW2_GRID_CELLS; c++) printf("%d", dw2_cell_heat(&me, c));
    printf(" B");
    for (int p = 0; p < me.placement_n; p++) printf(" %d", (int)(dw2_weapon_boost(&me, p) * 100.0f + 0.5f));
    printf(" L");
    for (int p = 0; p < me.placement_n; p++) {
        Dw2WeaponStats w = dw2_weapon_stats(me.placements[p].item_id);
        if (w.kind != DW2_SHOT_LASER) { printf(" -"); continue; }
        printf(" %d", dw2_lane_clear_ahead(&me, me.placements[p].cells[0].row * DW2_GRID_W + me.placements[p].cells[0].col));
    }
    printf(" O");
    for (int kind = 1; kind <= 3; kind++) for (int lane = 0; lane < DW2_GRID_W; lane++) printf(" %d", dw2_lane_shots_to_open(&me, lane, kind));
    printf("\n");
    /* the pieces each splittable part becomes, in place */
    for (int p = 0; p < me.placement_n; p++) {
        Dw2SplitPiece pc[4];
        int np = dw2_split_pieces(me.placements[p].item_id, me.placements[p].rotation, me.placements[p].anchor_row, me.placements[p].anchor_col, pc);
        if (np > 0) {
            printf("P %d %d", p, np);
            for (int k = 0; k < np; k++) printf(" %d,%d,%d,%d", pc[k].item, pc[k].row, pc[k].col, pc[k].rotation);
            printf("\n");
        }
    }
    /* the schedule of destroyed cells: up to two, at ticks 4..24, on cells that hold something */
    int kills = (int)(rnd() % 3), kt[2] = {0, 0}, kc[2] = {0, 0};
    for (int k = 0; k < kills; k++) {
        kt[k] = 4 + (int)(rnd() % 21);
        for (int tries = 0; tries < 40; tries++) { int c = (int)(rnd() % DW2_GRID_CELLS); if (me.cell_max[c] > 0.0f) { kc[k] = c; break; } kt[k] = 0; }
        if (kt[k]) printf("K %d %d\n", kt[k], kc[k]);
    }
    for (int t = 1; t <= 30; t++) {
        for (int k = 0; k < kills; k++) if (kt[k] == t) { me.cell_hp[kc[k]] = 0.0f; me.dead |= 1ull << kc[k]; }
        Dw2EventLog log; log.n = 0; log.dropped = 0;
        dw2_ship_tick(&me, &foe, &log);
        int shots = 0;
        for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_FIRE && log.ev[i].side == 0) shots++;
        /* the charge held in capacitors, batteries and delayers, and the open transistor gates: so a wrong STATE is seen the tick it appears */
        double q = 0; int gates = 0;
        for (int c = 0; c < DW2_GRID_CELLS; c++) { q += me.cell_q[c] + me.cell_q2[c]; gates += me.gate_open[c] ? (1 << (c % 20)) : 0; }
        /* parts the second's events destroyed (heat): a client is TOLD about these by EVENTS, it does not work them out */
        char lost[128]; lost[0] = 0;
        for (int i = 0; i < log.n; i++) if (log.ev[i].type == DW2_EV_HEAT && log.ev[i].d == 1) { char b[8]; snprintf(b, sizeof b, "%s%d", lost[0] ? "," : "", log.ev[i].a); strcat(lost, b); }
        printf("T %d %d %d %llx %llx %d %d %s\n", t, shots, me.shatter_events, (unsigned long long)me.burned, (unsigned long long)me.dead, (int)(q * 100.0 + 0.5), gates, lost[0] ? lost : "-");
    }
}

static void try_add(Ship *lo, int item, int row, int col, int rot) {
    if (lo->n >= MAXOPS) return;
    Dw2Ship t; dw2_ship_init(&t);
    for (int j = 0; j < lo->n; j++) if (dw2_ship_place(&t, lo->op[j].item, lo->op[j].row, lo->op[j].col, lo->op[j].rot) < 0) return;
    if (dw2_ship_place(&t, item, row, col, rot) >= 0) { lo->op[lo->n].item = item; lo->op[lo->n].row = row; lo->op[lo->n].col = col; lo->op[lo->n].rot = rot; lo->n++; }
}

static Ship chain_ship(void) {
    static const int parts[] = { 1, 1, 9, 12, 13, 14, 15, 15, 16, 17, 18, 18, 2, 7 };
    Ship lo; lo.n = 0;
    int row = (int)(rnd() % 6), len = 2 + (int)(rnd() % 4);
    try_add(&lo, 0, row, 0, 0);
    for (int k = 1; k <= len && k < 5; k++) {
        int it = parts[rnd() % (sizeof parts / sizeof parts[0])];
        int rot = (rnd() % 8 == 0) ? (int)(rnd() % 4) : 0;
        try_add(&lo, it, row, k, rot);
        if (it == 17 && row > 0) try_add(&lo, 0, row - 1, k, 1);   /* a generator above, pointing down into the gate */
        if (it == 17 && row > 1 && rnd() % 2) try_add(&lo, 12 + (int)(rnd() % 3), row - 1, k, 1);
    }
    int wp[] = { 3, 3, 7, 8, 10, 11 };
    try_add(&lo, wp[rnd() % 6], row, len + 1 > 5 ? 5 : len + 1, 0);
    if (rnd() % 2) try_add(&lo, 5, (row + 1) % 6, 1 + (int)(rnd() % 3), 0);
    if (rnd() % 2) try_add(&lo, 6, (row + 2) % 6, (int)(rnd() % 4), 0);
    if (rnd() % 3 == 0) try_add(&lo, 19, (int)(rnd() % 5), (int)(rnd() % 5), (int)(rnd() % 4));
    if (rnd() % 3 == 0) try_add(&lo, 20, (int)(rnd() % 6), (int)(rnd() % 5), (int)(rnd() % 4));
    if (rnd() % 3 == 0) try_add(&lo, 21, (int)(rnd() % 6), (int)(rnd() % 5), (int)(rnd() % 4));
    for (int k = 0, n = (int)(rnd() % 4); k < n; k++) try_add(&lo, 4, (int)(rnd() % 6), (int)(rnd() % 6), 0);
    if (rnd() % 3 == 0) try_add(&lo, 0, (int)(rnd() % 6), (int)(rnd() % 6), (int)(rnd() % 4));
    return lo;
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 300;
    if (argc > 2) rs ^= (uint64_t)atoll(argv[2]) * 0x2545F4914F6CDD1Dull;
    for (int i = 0; i < DW2_BOT_LOADOUT_COUNT; i++) {
        Ship s; s.n = dw2_bot_loadouts[i].n;
        for (int k = 0; k < s.n; k++) { s.op[k].item = dw2_bot_loadouts[i].op[k].item; s.op[k].row = dw2_bot_loadouts[i].op[k].row; s.op[k].col = dw2_bot_loadouts[i].op[k].col; s.op[k].rot = dw2_bot_loadouts[i].op[k].rot; }
        dump(i, &s);
    }
    /* the textbook ring: a generator feeding four splitters round a 2x2 (A (0,1) -> B (0,2) -> C (1,2) -> D (1,1) -> A) */
    static const Ship ring = { 5, { {0,0,0,0}, {2,0,1,3}, {2,0,2,3}, {2,1,2,0}, {2,1,1,1} } };
    dump(900, &ring);
    int dense = argc > 3 ? atoi(argv[3]) : 600;
    for (int i = 0; i < n + dense; i++) {
        int d = i >= n;
        Ship lo; lo.n = 0;
        int tries = 20 + (int)(rnd() % 60);
        for (int k = 0; k < tries && lo.n < MAXOPS; k++) {
            if (d) try_add(&lo, dense_pool[rnd() % 32], (int)(rnd() % 3), (int)(rnd() % 3), (int)(rnd() % 4));
            else try_add(&lo, (int)(rnd() % DW2_ITEM_COUNT), (int)(rnd() % 6), (int)(rnd() % 6), (int)(rnd() % 4));
        }
        dump(1000 + i, &lo);
    }
    int chains = argc > 4 ? atoi(argv[4]) : 800;
    for (int i = 0; i < chains; i++) { Ship lo = chain_ship(); dump(5000 + i, &lo); }
    return 0;
}
