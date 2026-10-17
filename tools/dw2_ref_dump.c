/* dw2_ref_dump -- prints what the real combat core does with a set of ships, so another implementation of the self half of the fight
 * (the D2 web client's wasm port, PARENA/stdlib/d2/web/d2_rules.prn) can be checked against it tick by tick.
 *
 * For each ship (the bot pool's, then N random legal layouts): the ops, then for each of 30 ticks the shots the ship fired, the
 * Back-EMF meltdowns so far and the mask of burned cells. The enemy is a ship with an enormous armor so every shot is counted
 * (a shot is 15 damage to it) and nothing ends the fight early. Damage is read off both the enemy's armor and its hull (a Pulse Laser skips
 * the armor), and the ship under test starts at 40 hull and 0 armor so the Mender's and the Aegis Emitter's work shows.
 *
 * The second batch is dense wiring (generators, conductors, splitters and weapons in a small corner), which is where closed loops and
 * Back-EMF meltdowns come from; uniformly random ships almost never have one.
 *
 * The third batch is structured circuits: a generator, a chain of random circuit parts along a row, a weapon at the end, sometimes a second
 * generator feeding a transistor's gate from above and a Titanium Beam or Aether Ore laid beside the guns and armor. Random placement almost never
 * builds a working Capacitor / Transistor / Delayer circuit, and these are what exercise them.
 *
 *   dw2_ref_dump [random-count] [seed] [dense-count] [chain-count] > ref.txt */
#include "../core/combat.h"
#include "../apps/bot/loadouts.h"
#include <stdio.h>
#include <stdlib.h>

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

/* splitters make rings; a ring is a closed loop and (after a few ticks) a meltdown */
static const int dense_pool[32] = { 0, 0, 2, 2, 2, 1, 1, 9, 3, 7, 8, 10, 11, 12, 13, 14, 15, 15, 16, 17, 17, 18, 18, 3, 3, 4, 5, 6, 2, 1, 0, 9 };

static void dump(int idx, const Dw2BotLoadout *lo) {
    Dw2Ship me, foe;
    dw2_ship_init(&me); dw2_ship_init(&foe);
    int placed[16][5], pn = 0;
    for (int k = 0; k < lo->n; k++) {
        if (dw2_ship_place(&me, lo->op[k].item, lo->op[k].row, lo->op[k].col, lo->op[k].rot) >= 0) {
            placed[pn][0] = lo->op[k].item; placed[pn][1] = lo->op[k].row; placed[pn][2] = lo->op[k].col; placed[pn][3] = lo->op[k].rot; placed[pn][4] = 0; pn++;
        }
    }
    for (int k = 0; k < pn; k++) if (lo->op[k].cut && dw2_ship_panic_cut(&me, k)) placed[k][4] = 1;
    printf("S %d %d", idx, pn);
    for (int k = 0; k < pn; k++) printf(" %d,%d,%d,%d,%d", placed[k][0], placed[k][1], placed[k][2], placed[k][3], placed[k][4]);
    printf("\n");
    dw2_ship_start_combat(&me); dw2_ship_start_combat(&foe);
    foe.armor = 1000000.0f; foe.hull_pct = 1000000.0f;
    printf("A %d %d\n", (int)me.armor, me.cargo_value);
    me.hull_pct = 40.0f; me.armor = 0.0f;
    for (int t = 1; t <= 30; t++) {
        float ba = foe.armor, bh = foe.hull_pct;
        dw2_ship_tick(&me, &foe);
        int dmg = (int)((ba - foe.armor) + (bh - foe.hull_pct) + 0.5f);
        unsigned long long burned = (unsigned long long)me.burned;
        /* the charge held in capacitors, batteries and delayers, and the open transistor gates: so a wrong STATE is seen the tick it appears */
        double q = 0; int gates = 0;
        for (int c = 0; c < DW2_GRID_CELLS; c++) { q += me.cell_q[c] + me.cell_q2[c]; gates += me.gate_open[c] ? (1 << (c % 20)) : 0; }
        printf("T %d %d %d %llx %d %d %d %d\n", t, dmg, me.shatter_events, burned, (int)(me.hull_pct + 0.5f), (int)(me.armor + 0.5f), (int)(q * 100.0 + 0.5), gates);
    }
}


static void try_add(Dw2BotLoadout *lo, Dw2BotOp o) {
    if (lo->n >= 16) return;
    Dw2Ship t; dw2_ship_init(&t);
    for (int j = 0; j < lo->n; j++) if (dw2_ship_place(&t, lo->op[j].item, lo->op[j].row, lo->op[j].col, lo->op[j].rot) < 0) return;
    if (dw2_ship_place(&t, o.item, o.row, o.col, o.rot) >= 0) lo->op[lo->n++] = o;
}

static Dw2BotLoadout chain_ship(void) {
    static const int parts[] = { 1, 1, 9, 12, 13, 14, 15, 15, 16, 17, 18, 18, 2, 7 };
    Dw2BotLoadout lo; lo.n = 0;
    int row = (int)(rnd() % 6), len = 2 + (int)(rnd() % 4);
    try_add(&lo, (Dw2BotOp){ 0, row, 0, 0, 0 });
    for (int k = 1; k <= len && k < 5; k++) {
        int it = parts[rnd() % (sizeof parts / sizeof parts[0])];
        int rot = (rnd() % 8 == 0) ? (int)(rnd() % 4) : 0;
        try_add(&lo, (Dw2BotOp){ it, row, k, rot, 0 });
        if (it == 17 && row > 0) try_add(&lo, (Dw2BotOp){ 0, row - 1, k, 1, 0 });   /* a generator above, pointing down into the gate */
        if (it == 17 && row > 1 && rnd() % 2) try_add(&lo, (Dw2BotOp){ 12 + (int)(rnd() % 3), row - 1, k, 1, 0 });
    }
    int wp[] = { 3, 3, 7, 8, 10, 11 };
    try_add(&lo, (Dw2BotOp){ wp[rnd() % 6], row, len + 1 > 5 ? 5 : len + 1, 0, 0 });
    if (rnd() % 2) try_add(&lo, (Dw2BotOp){ 5, (row + 1) % 6, 1 + (int)(rnd() % 3), 0, (rnd() % 3) == 0 });
    if (rnd() % 2) try_add(&lo, (Dw2BotOp){ 6, (row + 2) % 6, (int)(rnd() % 4), 0, (rnd() % 3) == 0 });
    for (int k = 0, n = (int)(rnd() % 4); k < n; k++) try_add(&lo, (Dw2BotOp){ 4, (int)(rnd() % 6), (int)(rnd() % 6), 0, 0 });
    if (rnd() % 3 == 0) try_add(&lo, (Dw2BotOp){ 0, (int)(rnd() % 6), (int)(rnd() % 6), (int)(rnd() % 4), 0 });
    return lo;
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 300;
    if (argc > 2) rs ^= (uint64_t)atoll(argv[2]) * 0x2545F4914F6CDD1Dull;
    for (int i = 0; i < DW2_BOT_LOADOUT_COUNT; i++) dump(i, &dw2_bot_loadouts[i]);
    /* the textbook ring: a generator feeding four splitters round a 2x2 (A (0,1) -> B (0,2) -> C (1,2) -> D (1,1) -> A) */
    static const Dw2BotLoadout ring = { 5, { {0,0,0,0,0}, {2,0,1,3,0}, {2,0,2,3,0}, {2,1,2,0,0}, {2,1,1,1,0} } };
    dump(900, &ring);
    int dense = argc > 3 ? atoi(argv[3]) : 600;
    for (int i = 0; i < n + dense; i++) {
        int d = i >= n;
        Dw2BotLoadout lo; lo.n = 0;
        int tries = 20 + (int)(rnd() % 40);
        for (int k = 0; k < tries && lo.n < 16; k++) {
            Dw2BotOp o = d ? (Dw2BotOp){ dense_pool[rnd() % 32], (int)(rnd() % 3), (int)(rnd() % 3), (int)(rnd() % 4), 0 }
                           : (Dw2BotOp){ (int)(rnd() % DW2_ITEM_COUNT), (int)(rnd() % 6), (int)(rnd() % 6), (int)(rnd() % 4), (rnd() % 3) == 0 };
            Dw2Ship t; dw2_ship_init(&t);
            int ok = 1;
            for (int j = 0; j < lo.n && ok; j++) ok = dw2_ship_place(&t, lo.op[j].item, lo.op[j].row, lo.op[j].col, lo.op[j].rot) >= 0;
            if (ok && dw2_ship_place(&t, o.item, o.row, o.col, o.rot) >= 0) lo.op[lo.n++] = o;
        }
        dump(1000 + i, &lo);
    }
    int chains = argc > 4 ? atoi(argv[4]) : 800;
    for (int i = 0; i < chains; i++) { Dw2BotLoadout lo = chain_ship(); dump(5000 + i, &lo); }
    return 0;
}
