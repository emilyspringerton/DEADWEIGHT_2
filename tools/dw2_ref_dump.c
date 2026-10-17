/* dw2_ref_dump -- prints what the real combat core does with a set of ships, so another implementation of the self half of the fight
 * (the D2 web client's wasm port, PARENA/stdlib/d2/web/d2_rules.prn) can be checked against it tick by tick.
 *
 * For each ship (the bot pool's, then N random legal layouts): the ops, then for each of 30 ticks the shots the ship fired, the
 * Back-EMF meltdowns so far and the mask of burned cells. The enemy is a ship with an enormous armor so every shot is counted
 * (a shot is 15 damage to it) and nothing ends the fight early.
 *
 * The second batch is dense wiring (generators, conductors, splitters and weapons in a small corner), which is where closed loops and
 * Back-EMF meltdowns come from; uniformly random ships almost never have one.
 *
 *   dw2_ref_dump [random-count] [seed] [dense-count] > ref.txt */
#include "../core/combat.h"
#include "../apps/bot/loadouts.h"
#include <stdio.h>
#include <stdlib.h>

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

/* splitters make rings; a ring is a closed loop and (after a few ticks) a meltdown */
static const int dense_pool[8] = { 0, 2, 2, 2, 2, 1, 3, 2 };

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
    foe.armor = 1000000.0f;
    printf("A %d %d\n", (int)me.armor, me.cargo_value);
    for (int t = 1; t <= 30; t++) {
        float before = foe.armor;
        dw2_ship_tick(&me, &foe);
        int shots = (int)((before - foe.armor) / 15.0f + 0.5f);
        unsigned long long burned = (unsigned long long)me.burned;
        printf("T %d %d %d %llx\n", t, shots, me.shatter_events, burned);
    }
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
            Dw2BotOp o = d ? (Dw2BotOp){ dense_pool[rnd() % 8], (int)(rnd() % 3), (int)(rnd() % 3), (int)(rnd() % 4), 0 }
                           : (Dw2BotOp){ (int)(rnd() % DW2_ITEM_COUNT), (int)(rnd() % 6), (int)(rnd() % 6), (int)(rnd() % 4), (rnd() % 3) == 0 };
            Dw2Ship t; dw2_ship_init(&t);
            int ok = 1;
            for (int j = 0; j < lo.n && ok; j++) ok = dw2_ship_place(&t, lo.op[j].item, lo.op[j].row, lo.op[j].col, lo.op[j].rot) >= 0;
            if (ok && dw2_ship_place(&t, o.item, o.row, o.col, o.rot) >= 0) lo.op[lo.n++] = o;
        }
        dump(1000 + i, &lo);
    }
    return 0;
}
