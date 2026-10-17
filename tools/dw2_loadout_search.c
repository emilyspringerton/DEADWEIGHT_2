/* dw2_loadout_search -- finds strong packed ships for the D2 bot pool (apps/bot/).
 *
 * A bot needs to arrive at a match with a ship that is actually good, and D2 has no inventory
 * budget (any item can be placed any number of times, up to DW2_MAX_PLACEMENTS), so "good" is a
 * pure search problem over the 6x6 grid. This tool does that search against the REAL combat core
 * (core/combat.c, the same dw2_ship_place / dw2_ship_panic_cut / dw2_ship_tick / dw2_match_result
 * dw2_server runs): a co-evolving population of random legal layouts, each scored by playing full
 * 30-tick matches against a rotating field of opponents (random layouts plus the hall of fame so
 * far), mutate-and-keep-the-best for a few dozen generations, then a round robin among the finalists
 * picks the pool. The round-break mini-game is left out of the scoring on purpose: a call is the
 * player's skill at the moment, not part of the layout.
 *
 *   gcc ... tools/dw2_loadout_search.c core/items.c core/combat.c core/round.c $CANNON_SRC -o dw2_loadout_search
 *   ./dw2_loadout_search [--seed N] [--gens N] [--pop N] [--out apps/bot/loadouts.h]
 *
 * The output (apps/bot/loadouts.h) is committed, generated; regenerate it when the item catalog or
 * the combat rules change. Nothing here ships in a release binary. */
#include "../core/combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXOPS 16
typedef struct { int item, row, col, rot, cut; } Op;
typedef struct { Op op[MAXOPS]; int n; double fit; } Lo;

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static int rn(int n) { return (int)(rnd() % (uint32_t)n); }

/* Builds the ship the way the server will see it: place in order, then cuts. Ops that turn out
 * illegal are dropped from `lo` so every Lo handed around is valid by construction. */
static void build(Lo *lo, Dw2Ship *s) {
    dw2_ship_init(s);
    Op kept[MAXOPS]; int kn = 0;
    for (int i = 0; i < lo->n; i++) {
        Op o = lo->op[i];
        if (dw2_ship_place(s, o.item, o.row, o.col, o.rot) >= 0) kept[kn++] = o;
    }
    for (int i = 0; i < kn; i++)
        if (kept[i].cut && !dw2_ship_panic_cut(s, i)) kept[i].cut = 0;
    memcpy(lo->op, kept, sizeof kept); lo->n = kn;
}

/* 1 = a wins, 0.5 = tie, 0 = loss, plus a small hull-margin term so the search sees progress. */
static double duel(const Lo *a, const Lo *b) {
    Lo la = *a, lb = *b; Dw2Ship sa, sb;
    build(&la, &sa); build(&lb, &sb);
    dw2_ship_start_combat(&sa); dw2_ship_start_combat(&sb);
    int t = 0; Dw2MatchResult r = DW2_RESULT_ONGOING;
    while (r == DW2_RESULT_ONGOING) {
        dw2_ship_tick(&sa, &sb); dw2_ship_tick(&sb, &sa); t++;
        r = dw2_match_result(&sa, &sb, t);
    }
    double m = (sa.hull_pct - sb.hull_pct) / 400.0;
    return (r == DW2_RESULT_SELF_WIN ? 1.0 : r == DW2_RESULT_TIE ? 0.5 : 0.0) + m;
}

static void random_op(Op *o) {
    o->item = rn(DW2_ITEM_COUNT); o->row = rn(6); o->col = rn(6); o->rot = rn(4); o->cut = rn(3) == 0;
}
static void random_lo(Lo *lo) {
    memset(lo, 0, sizeof *lo);
    int tries = 20 + rn(60);
    for (int i = 0; i < tries && lo->n < MAXOPS; i++) {
        Dw2Ship s; Lo t = *lo; random_op(&t.op[t.n++]);
        build(&t, &s);
        if (t.n > lo->n) *lo = t;
    }
}
static void mutate(Lo *lo) {
    int k = 1 + rn(3);
    while (k--) {
        int c = rn(5);
        if (c == 0 && lo->n > 1) { int i = rn(lo->n); memmove(&lo->op[i], &lo->op[i + 1], (size_t)(lo->n - i - 1) * sizeof(Op)); lo->n--; }
        else if (c == 1 && lo->n < MAXOPS) random_op(&lo->op[lo->n++]);
        else if (c == 2 && lo->n) { Op *o = &lo->op[rn(lo->n)]; o->row = rn(6); o->col = rn(6); }
        else if (c == 3 && lo->n) { Op *o = &lo->op[rn(lo->n)]; o->rot = (o->rot + 1) % 4; }
        else if (lo->n) { Op *o = &lo->op[rn(lo->n)]; o->item = rn(DW2_ITEM_COUNT); o->cut = rn(3) == 0; }
    }
    Dw2Ship s; build(lo, &s);
}

static int cmp_fit(const void *x, const void *y) { double a = ((const Lo *)x)->fit, b = ((const Lo *)y)->fit; return a < b ? 1 : a > b ? -1 : 0; }
static int same(const Lo *a, const Lo *b) {
    Dw2Ship sa, sb; Lo la = *a, lb = *b; build(&la, &sa); build(&lb, &sb);
    return sa.grid.occupied == sb.grid.occupied && sa.armor == sb.armor && sa.placement_n == sb.placement_n;
}

int main(int argc, char **argv) {
    int gens = 80, pop = 160, seed = 7; const char *out = "apps/bot/loadouts.h";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gens") && i + 1 < argc) gens = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pop") && i + 1 < argc) pop = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }
    rs ^= (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    Lo *P = calloc((size_t)pop, sizeof *P), *H = calloc(64, sizeof *H); int hn = 0;
    for (int i = 0; i < pop; i++) random_lo(&P[i]);
    for (int g = 0; g < gens; g++) {
        for (int i = 0; i < pop; i++) {
            double sum = 0; int games = 28;
            for (int k = 0; k < games; k++) {
                const Lo *o = (hn && k % 3 == 0) ? &H[rn(hn)] : &P[rn(pop)];
                sum += duel(&P[i], o);
            }
            P[i].fit = sum / games;
        }
        qsort(P, (size_t)pop, sizeof *P, cmp_fit);
        if (hn < 64) H[hn++] = P[0]; else H[rn(64)] = P[0];
        if (g % 10 == 0 || g == gens - 1) fprintf(stderr, "gen %3d best %.3f median %.3f (%d ops)\n", g, P[0].fit, P[pop / 2].fit, P[0].n);
        int keep = pop / 4;
        for (int i = keep; i < pop; i++) { P[i] = P[rn(keep)]; mutate(&P[i]); }
    }
    /* finalists: the distinct top of the last generation plus the hall of fame, round robin */
    Lo F[256]; int fn = 0;
    for (int i = 0; i < pop && fn < 200; i++) { int dup = 0; for (int j = 0; j < fn; j++) if (same(&P[i], &F[j])) { dup = 1; break; } if (!dup) F[fn++] = P[i]; }
    for (int i = 0; i < hn && fn < 256; i++) { int dup = 0; for (int j = 0; j < fn; j++) if (same(&H[i], &F[j])) { dup = 1; break; } if (!dup) F[fn++] = H[i]; }
    for (int i = 0; i < fn; i++) { double s = 0; for (int j = 0; j < fn; j++) if (i != j) s += duel(&F[i], &F[j]); F[i].fit = s / (fn - 1); }
    qsort(F, (size_t)fn, sizeof *F, cmp_fit);
    int want = 12 < fn ? 12 : fn;
    FILE *f = fopen(out, "w");
    if (!f) { perror(out); return 1; }
    fprintf(f, "/* GENERATED by tools/dw2_loadout_search.c -- do not hand-edit. The bot pool's ships: the top of a co-evolved\n"
               " * search over the real combat core (see that file). Each op is {item, row, col, rot, cut}; ops are placed in\n"
               " * order, then every op with cut=1 is Panic-Cut by its placement index. */\n"
               "#ifndef DW2_BOT_LOADOUTS_H\n#define DW2_BOT_LOADOUTS_H\n"
               "typedef struct { int item, row, col, rot, cut; } Dw2BotOp;\n"
               "typedef struct { int n; Dw2BotOp op[16]; } Dw2BotLoadout;\n"
               "static const Dw2BotLoadout dw2_bot_loadouts[%d] = {\n", want);
    for (int i = 0; i < want; i++) {
        Dw2Ship s; Lo l = F[i]; build(&l, &s);
        fprintf(f, "    { %d, { ", l.n);
        for (int k = 0; k < l.n; k++) fprintf(f, "{%d,%d,%d,%d,%d}%s", l.op[k].item, l.op[k].row, l.op[k].col, l.op[k].rot, l.op[k].cut, k + 1 < l.n ? ", " : "");
        fprintf(f, " } }, /* round-robin score %.3f */\n", F[i].fit);
    }
    fprintf(f, "};\n#define DW2_BOT_LOADOUT_COUNT %d\n#endif\n", want);
    fclose(f);
    fprintf(stderr, "wrote %d loadouts to %s\n", want, out);
    return 0;
}
