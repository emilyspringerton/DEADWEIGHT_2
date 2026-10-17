/* dw2_loadout_search -- finds strong packed ships for the D2 bot pool (apps/bot/), and checks that no single strategy rules.
 *
 * A bot needs to arrive at a match with a ship that is actually good. A ship is a layout of parts on the 6x6 grid under a credit and a weight budget (core/items.h),
 * so "good" is a search problem. This tool does that search against the REAL combat core (core/combat.c, the same dw2_ship_place / dw2_match_tick /
 * dw2_match_result dw2_server runs): a co-evolving population of random legal layouts, each scored by playing full 30-tick matches against a rotating field of
 * opponents (random layouts plus the hall of fame so far), mutate-and-keep-the-best for a few dozen generations, then a round robin among the finalists picks the
 * pool. The round-break mini-game is left out of the scoring on purpose: a call is the player's skill at the moment, not part of the layout.
 *
 *   gcc ... tools/dw2_loadout_search.c core/items.c core/combat.c core/round.c $CANNON_SRC -o dw2_loadout_search
 *   ./dw2_loadout_search [--seed N] [--gens N] [--pop N] [--out apps/bot/loadouts.h]
 *   ./dw2_loadout_search --archetypes      a round robin of hand-built ships (the strategies players reach for), with the result matrix
 *   ./dw2_loadout_search --survey [--seed N] ...  runs the search, then reports what the strongest ships are MADE of and how often each weapon type wins
 *
 * The output (apps/bot/loadouts.h) is committed, generated; regenerate it when the item catalog or the combat rules change. Nothing here ships in a release binary. */
#include "../core/combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXOPS 36
typedef struct { int item, row, col, rot; } Op;
typedef struct { Op op[MAXOPS]; int n; double fit; int fam; } Lo;   /* fam: the one weapon family the ship may use (1 laser, 2 rail, 3 cannon) */

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static int rn(int n) { return (int)(rnd() % (uint32_t)n); }

/* Builds the ship the way the server will see it: place in order. Ops that turn out illegal are dropped from `lo` so every Lo handed around is valid by construction. */
static void build(Lo *lo, Dw2Ship *s) {
    dw2_ship_init(s);
    Op kept[MAXOPS]; int kn = 0;
    for (int i = 0; i < lo->n; i++) {
        Op o = lo->op[i];
        if (dw2_ship_place(s, o.item, o.row, o.col, o.rot) >= 0) kept[kn++] = o;
    }
    memcpy(lo->op, kept, sizeof kept); lo->n = kn;
}

/* 1 = a wins, 0.5 = tie, 0 = loss, plus a small hull-margin term so the search sees progress. Returns the ticks the match lasted through *ticks. */
static double duel(const Lo *a, const Lo *b, int *ticks) {
    Lo la = *a, lb = *b; Dw2Ship sa, sb;
    build(&la, &sa); build(&lb, &sb);
    sb.idx = 1;
    dw2_ship_start_combat(&sa); dw2_ship_start_combat(&sb);
    int t = 0; Dw2MatchResult r = DW2_RESULT_ONGOING;
    while (r == DW2_RESULT_ONGOING) {
        dw2_match_tick(&sa, &sb, t, NULL); t++;
        r = dw2_match_result(&sa, &sb, t);
    }
    if (ticks) *ticks = t;
    double m = (sa.hull_pct - sb.hull_pct) / 400.0;
    return (r == DW2_RESULT_SELF_WIN ? 1.0 : r == DW2_RESULT_TIE ? 0.5 : 0.0) + m;
}

static int cur_fam = 0;     /* the species being bred: weapons of the other families are not drawn */
static int fam_item(void) {
    for (;;) {
        int it = rn(DW2_ITEM_COUNT);
        Dw2WeaponStats w = dw2_weapon_stats(it);
        if (cur_fam && w.kind && w.kind != cur_fam) continue;
        return it;
    }
}
static void random_op(Op *o) {
    o->item = fam_item(); o->row = rn(6); o->col = rn(6); o->rot = rn(4);
}
static int try_add(Lo *lo, Op o) {
    if (lo->n >= MAXOPS) return 0;
    Lo t = *lo; t.op[t.n++] = o;
    Dw2Ship s; build(&t, &s);
    if (t.n > lo->n) { *lo = t; return 1; }
    return 0;
}
/* a random ship that probably WORKS: a few feed rows (a Generator pushing east through a few wires into a weapon), then random filler. A uniformly random layout
 * almost never wires a gun, and a search that starts there only learns to sit still. */
static void random_lo(Lo *lo) {
    memset(lo, 0, sizeof *lo);
    int rows = 1 + rn(4);
    static const int guns[] = { DW2_ITEM_PULSE_LASER, DW2_ITEM_PULSE_LASER, DW2_ITEM_RAILGUN, DW2_ITEM_RAILGUN, DW2_ITEM_SIEGE_CANNON };
    for (int k = 0; k < rows; k++) {
        int r = rn(6), len = rn(3), w = guns[rn(5)];
        if (cur_fam) w = cur_fam == 1 ? DW2_ITEM_PULSE_LASER : cur_fam == 2 ? DW2_ITEM_RAILGUN : DW2_ITEM_SIEGE_CANNON;
        try_add(lo, (Op){ DW2_ITEM_GENERATOR, r, 0, 0 });
        for (int c = 1; c <= len; c++) try_add(lo, (Op){ rn(4) ? DW2_ITEM_CONDUCTOR : DW2_ITEM_AMPLIFIER, r, c, 0 });
        try_add(lo, (Op){ w, r, len + 1, 0 });
    }
    int tries = 10 + rn(100);
    for (int i = 0; i < tries && lo->n < MAXOPS; i++) { Op o; random_op(&o); try_add(lo, o); }
}
static void mutate(Lo *lo) {
    int k = 1 + rn(3);
    while (k--) {
        int c = rn(5);
        if (c == 0 && lo->n > 1) { int i = rn(lo->n); memmove(&lo->op[i], &lo->op[i + 1], (size_t)(lo->n - i - 1) * sizeof(Op)); lo->n--; }
        else if (c == 1 && lo->n < MAXOPS) random_op(&lo->op[lo->n++]);
        else if (c == 2 && lo->n) { Op *o = &lo->op[rn(lo->n)]; o->row = rn(6); o->col = rn(6); }
        else if (c == 3 && lo->n) { Op *o = &lo->op[rn(lo->n)]; o->rot = (o->rot + 1) % 4; }
        else if (lo->n) { Op *o = &lo->op[rn(lo->n)]; o->item = fam_item(); }
    }
    Dw2Ship s; build(lo, &s);
    /* top the ship up with whatever still fits, so the search is not hobbled by half-empty holds */
    for (int i = 0; i < 12 && lo->n < MAXOPS; i++) { Lo t = *lo; random_op(&t.op[t.n++]); Dw2Ship u; build(&t, &u); if (t.n > lo->n) *lo = t; }
}

static int cmp_fit(const void *x, const void *y) { double a = ((const Lo *)x)->fit, b = ((const Lo *)y)->fit; return a < b ? 1 : a > b ? -1 : 0; }
static int same(const Lo *a, const Lo *b) {
    Dw2Ship sa, sb; Lo la = *a, lb = *b; build(&la, &sa); build(&lb, &sb);
    return sa.grid.occupied == sb.grid.occupied && sa.cost == sb.cost && sa.placement_n == sb.placement_n;
}

/* The strategies a person reaches for, drawn as in tools/dw2_trace.c (a letter and a rotation digit per cell; only the anchor of a part is written). Every candidate is
 * also scored against these, so the pool cannot be a set of ships that only know how to beat each other: a row of lasers on generators, a Titanium wall with railguns
 * behind it, a cannon behind plates, an Aether wall. */
static const char *ARCH[] = {
    "L3L3L3L3....\nG3G3G3G3....\nB0B0........\n............\n............\n............\n",
    "L3L3L3......\nG3G3G3......\nB0B0B0......\n............\n............\n............\n",
    "............\n......B0....\nT0..........\nG0R0G0R0....\n............\n............\n",
    "............\n............\n............\nG0C0G0..B0B0\nB0B0B0......\n............\n",
    "O0..O0......\n............\n............\nG0R0G0R0....\n............\n............\n",
    "B0B0B0B0B0..\n............\nG0R0........\nG0R0........\n............\n............\n",
    "............\n..L3..L3......\n..G3..G3......\nB0B0B0B0....\nG0C0........\n............\n",
    "............\n............\n............\nG0C0--......\n....G0C0--..\n....B0B0....\n",
    "............\n............\n............\nG0C0--..G0R0\nB0B0B0B0....\n............\n",
    "..B0..B0....\n............\n............\nG0C0--......\n....G0C0--..\n............\n",
    "..B0..B0....\n............\n..B0..B0....\nG0C0--......\n....G0C0--..\n............\n",
};
#define NARCH ((int)(sizeof ARCH / sizeof ARCH[0]))
static Lo archetype[NARCH];
static void parse_arch(void) {
    static const char *LET = "GcsRBTOLCAME124PYXDQVNto";
    for (int k = 0; k < NARCH; k++) {
        Lo *l = &archetype[k]; memset(l, 0, sizeof *l);
        int row = 0, col = 0;
        for (const char *p = ARCH[k]; *p; ) {
            if (*p == '\n') { row++; col = 0; p++; continue; }
            char a = p[0], b = p[1]; p += 2;
            if (a != '.' && a != '-') { const char *q = strchr(LET, a); l->op[l->n].item = (int)(q - LET); l->op[l->n].row = row; l->op[l->n].col = col; l->op[l->n].rot = b - '0'; l->n++; }
            col++;
        }
    }
}

/* what a ship is built around: the weapon kind it spends the most credits on (0 = none, 1 laser, 2 rail, 3 cannon) */
static const char *fam_name[4] = { "no-gun", "laser", "rail", "cannon" };
static int dominant(Lo *lo) {
    Dw2Ship s; build(lo, &s);
    int spend[4] = {0, 0, 0, 0};
    for (int p = 0; p < s.placement_n; p++) { Dw2WeaponStats w = dw2_weapon_stats(s.placements[p].item_id); if (w.kind) spend[w.kind] += dw2_catalog[s.placements[p].item_id].cost; }
    int best = 0;
    for (int k = 1; k < 4; k++) if (spend[k] > spend[best]) best = k;
    return spend[best] > 0 ? best : 0;
}

int main(int argc, char **argv) {
    int gens = 80, pop = 160, seed = 7, survey = 0, archmode = 0; const char *out = "apps/bot/loadouts.h";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gens") && i + 1 < argc) gens = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pop") && i + 1 < argc) pop = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--survey")) survey = 1;
        else if (!strcmp(argv[i], "--archetypes")) archmode = 1;
    }
    rs ^= (uint64_t)seed * 0x9E3779B97F4A7C15ull;
    parse_arch();
    if (archmode) {
        /* the hand-drawn strategies against each other: row's win rate against column (1 win, 0.5 draw) */
        fprintf(stderr, "archetype round robin (row wins vs column):\n    ");
        for (int j = 0; j < NARCH; j++) fprintf(stderr, " %4d", j);
        fprintf(stderr, "\n");
        for (int i = 0; i < NARCH; i++) {
            Dw2Ship sh; Lo l = archetype[i]; build(&l, &sh);
            fprintf(stderr, "%2d (%4d cr %2d t)", i, sh.cost, sh.weight);
            for (int j = 0; j < NARCH; j++) { if (i == j) { fprintf(stderr, "    -"); continue; } double d = duel(&archetype[i], &archetype[j], NULL); fprintf(stderr, " %4.1f", d >= 1.0 ? 1.0 : d >= 0.5 ? 0.5 : 0.0); }
            fprintf(stderr, "\n");
        }
        return 0;
    }
    /* three species (laser-only, rail-only, cannon-only ships) breed apart but are scored against all of them, so no family can crowd the others out */
    int S = 3, per = pop / S; pop = per * S;
    Lo *P = calloc((size_t)pop, sizeof *P), *H = calloc(64, sizeof *H); int hn = 0;
    for (int sp = 0; sp < S; sp++) { cur_fam = sp + 1; for (int i = 0; i < per; i++) { random_lo(&P[sp * per + i]); P[sp * per + i].fam = sp + 1; } }
    for (int g = 0; g < gens; g++) {
        for (int i = 0; i < pop; i++) {
            double sum = 0; int games = 30;
            for (int k = 0; k < games; k++) {
                const Lo *o = (k % 5 == 1) ? &archetype[rn(NARCH)] : (hn && k % 3 == 0) ? &H[rn(hn)] : &P[rn(pop)];
                sum += duel(&P[i], o, NULL);
            }
            P[i].fit = sum / games;
        }
        for (int sp = 0; sp < S; sp++) {
            Lo *Q = &P[sp * per];
            qsort(Q, (size_t)per, sizeof *Q, cmp_fit);
            if (hn < 64) H[hn++] = Q[0]; else H[rn(64)] = Q[0];
            cur_fam = sp + 1;
            int keep = per / 4;
            if (g % 10 == 0 || g == gens - 1) fprintf(stderr, "gen %3d species %d best %.3f median %.3f (%d ops)\n", g, sp + 1, Q[0].fit, Q[per / 2].fit, Q[0].n);
            if (g < gens - 1) for (int i = keep; i < per; i++) { Q[i] = Q[rn(keep)]; mutate(&Q[i]); Q[i].fam = sp + 1; }
        }
    }
    /* finalists: the distinct top of the last generation plus the hall of fame, round robin */
    Lo F[256]; int fn = 0;
    for (int i = 0; i < pop && fn < 200; i++) { int dup = 0; for (int j = 0; j < fn; j++) if (same(&P[i], &F[j])) { dup = 1; break; } if (!dup) F[fn++] = P[i]; }
    for (int i = 0; i < hn && fn < 256; i++) { int dup = 0; for (int j = 0; j < fn; j++) if (same(&H[i], &F[j])) { dup = 1; break; } if (!dup) F[fn++] = H[i]; }
    for (int i = 0; i < fn; i++) { double s = 0; for (int j = 0; j < fn; j++) if (i != j) s += duel(&F[i], &F[j], NULL); F[i].fit = s / (fn - 1); }
    qsort(F, (size_t)fn, sizeof *F, cmp_fit);
    if (survey) {
        /* what are the winners made of, and how do the families fare against each other? */
        static const char *fam[4] = { "no-gun", "laser", "rail", "cannon" };
        int top = fn / 4 > 0 ? fn / 4 : 1; int cnt[DW2_ITEM_COUNT] = {0};
        for (int i = 0; i < top; i++) { Dw2Ship s; Lo l = F[i]; build(&l, &s); for (int p = 0; p < s.placement_n; p++) cnt[s.placements[p].item_id]++; }
        fprintf(stderr, "top %d of %d finalists, parts per ship:\n", top, fn);
        for (int k = 0; k < DW2_ITEM_COUNT; k++) fprintf(stderr, "  %-15s %.2f\n", dw2_catalog[k].name, (double)cnt[k] / top);
        double M[4][4] = {{0}}; int N[4][4] = {{0}}; int pop4[4] = {0}; int ticks_sum = 0, ticks_n = 0, wins_by_hull = 0;
        for (int i = 0; i < fn; i++) pop4[dominant(&F[i])]++;
        for (int i = 0; i < fn; i++) for (int j = 0; j < fn; j++) if (i != j) {
            int t; double d = duel(&F[i], &F[j], &t); int a2 = dominant(&F[i]), b2 = dominant(&F[j]);
            M[a2][b2] += d > 1.0 ? 1.0 : d < 0.0 ? 0.0 : d >= 0.5 ? (d >= 1.0 ? 1.0 : 0.5) : 0.0; N[a2][b2]++;
            ticks_sum += t; ticks_n++; if (t < 30) wins_by_hull++;
        }
        fprintf(stderr, "finalists by family:"); for (int k = 0; k < 4; k++) fprintf(stderr, " %s=%d", fam[k], pop4[k]); fprintf(stderr, "\n");
        fprintf(stderr, "win rate of ROW family vs COLUMN family (finalists):\n          "); for (int k = 0; k < 4; k++) fprintf(stderr, "%8s", fam[k]); fprintf(stderr, "\n");
        for (int a2 = 0; a2 < 4; a2++) { fprintf(stderr, "%-10s", fam[a2]); for (int b2 = 0; b2 < 4; b2++) { if (N[a2][b2]) fprintf(stderr, "%8.2f", M[a2][b2] / N[a2][b2]); else fprintf(stderr, "%8s", "-"); } fprintf(stderr, "\n"); }
        fprintf(stderr, "average match %.1f ticks; %.0f%% ended before the clock\n", (double)ticks_sum / ticks_n, 100.0 * wins_by_hull / ticks_n);
        fprintf(stderr, "the archetypes against the finalists (their win rate):\n");
        for (int k = 0; k < NARCH; k++) { double w = 0; for (int j = 0; j < fn; j++) { double d = duel(&archetype[k], &F[j], NULL); w += d >= 1.0 ? 1.0 : d >= 0.5 ? 0.5 : 0.0; } fprintf(stderr, "  archetype %d: %.2f\n", k, w / fn); }
        static const char *LET = "GcsRBTOLCAME124PYXDQVNto";
        for (int b = 0; b < 4; b++) {
            Dw2Ship s; Lo l = F[b]; build(&l, &s);
            char g[6][6][3];
            for (int r = 0; r < 6; r++) for (int c = 0; c < 6; c++) strcpy(g[r][c], "..");
            for (int p = 0; p < s.placement_n; p++) {
                int first = 1;
                for (int c = 0; c < s.placements[p].cell_n; c++) {
                    const Dw2PlacedCell *pc = &s.placements[p].cells[c];
                    if (first && pc->row == s.placements[p].anchor_row && pc->col == s.placements[p].anchor_col) { g[pc->row][pc->col][0] = LET[s.placements[p].item_id]; g[pc->row][pc->col][1] = (char)('0' + s.placements[p].rotation % 4); first = 0; }
                    else strcpy(g[pc->row][pc->col], "--");
                }
            }
            fprintf(stderr, "# ship %d score %.3f cost %d weight %d (%s)\n", b, F[b].fit, s.cost, s.weight, fam[dominant(&F[b])]);
            for (int r = 0; r < 6; r++) { for (int c = 0; c < 6; c++) fprintf(stderr, "%s", g[r][c]); fprintf(stderr, "\n"); }
        }
    }
    /* the pool: the human strategies drawn above that make a good opponent (each beats some and loses to others: see --archetypes), then the best evolved ships --
     * the ones that do best against the archetypes as well as each other -- so a player meets variety, and every opponent has a counter they can build */
    static const int POOL_ARCH[] = { 0, 1, 2, 4, 7, 8, 9 };
    Lo pool[24]; int want = 0; int is_arch[24] = {0};
    for (int k = 0; k < (int)(sizeof POOL_ARCH / sizeof POOL_ARCH[0]); k++) { pool[want] = archetype[POOL_ARCH[k]]; is_arch[want] = 1 + POOL_ARCH[k]; want++; }
    for (int i = 0; i < fn; i++) {      /* a finalist's own score plus its win rate against the archetypes */
        double w = 0; for (int k = 0; k < NARCH; k++) { double d = duel(&F[i], &archetype[k], NULL); w += d >= 1.0 ? 1.0 : d >= 0.5 ? 0.5 : 0.0; }
        F[i].fit = F[i].fit + w / NARCH;
    }
    qsort(F, (size_t)fn, sizeof *F, cmp_fit);
    for (int fam = 1; fam <= 3 && want < 12; fam++) {
        for (int i = 0; i < fn; i++) {
            if (dominant(&F[i]) != fam) continue;
            int dup = 0; for (int j = 0; j < want; j++) if (same(&F[i], &pool[j])) dup = 1;
            if (dup) continue;
            pool[want++] = F[i]; break;
        }
    }
    for (int i = 0; i < fn && want < 12; i++) {
        int dup = 0; for (int j = 0; j < want; j++) if (same(&F[i], &pool[j])) dup = 1;
        if (!dup) pool[want++] = F[i];
    }
    FILE *f = fopen(out, "w");
    if (!f) { perror(out); return 1; }
    fprintf(f, "/* GENERATED by tools/dw2_loadout_search.c -- do not hand-edit. The bot pool's ships: the top of a co-evolved\n"
               " * search over the real combat core (see that file). Each op is {item, row, col, rot}; ops are placed in order. */\n"
               "#ifndef DW2_BOT_LOADOUTS_H\n#define DW2_BOT_LOADOUTS_H\n"
               "typedef struct { int item, row, col, rot; } Dw2BotOp;\n"
               "typedef struct { int n; Dw2BotOp op[36]; } Dw2BotLoadout;\n"
               "static const Dw2BotLoadout dw2_bot_loadouts[%d] = {\n", want);
    for (int i = 0; i < want; i++) {
        Dw2Ship s; Lo l = pool[i]; build(&l, &s);
        fprintf(f, "    { %d, { ", l.n);
        for (int k = 0; k < l.n; k++) fprintf(f, "{%d,%d,%d,%d}%s", l.op[k].item, l.op[k].row, l.op[k].col, l.op[k].rot, k + 1 < l.n ? ", " : "");
        if (is_arch[i]) fprintf(f, " } }, /* %s, strategy %d, %d credits %d t */\n", fam_name[dominant(&pool[i])], is_arch[i] - 1, s.cost, s.weight);
        else fprintf(f, " } }, /* %s, evolved, score %.3f, %d credits %d t */\n", fam_name[dominant(&pool[i])], pool[i].fit, s.cost, s.weight);
    }
    fprintf(f, "};\n#define DW2_BOT_LOADOUT_COUNT %d\n#endif\n", want);
    fclose(f);
    fprintf(stderr, "wrote %d loadouts to %s\n", want, out);
    return 0;
}
