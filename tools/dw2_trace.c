/* dw2_trace -- builds two ships from ASCII art and plays them against each other, printing what happens each second. For balance work and debugging.
 *
 *   dw2_trace SHIP_A SHIP_B [-q]        each is a file of 6 rows of 6 two-character cells, or a built-in name (see archetypes below)
 *
 * A cell is a letter and a rotation digit (0-3, quarter turns clockwise): ".." empty, "--" covered by the part to its left or above (multi-cell parts are written
 * once, at their top-left cell), otherwise
 *   G generator  c conductor  s splitter  R railgun  B bulwark  T titanium beam  O aether ore  L pulse laser  C siege cannon  A amplifier  M mender  E aegis
 *   1 2 4 resistors  P capacitor  Y battery  X transistor  D delayer  Q quantum core  V cryo-vent  N xeno-biomass  t titanium shard  o ore chip
 * Row 0 is the bow (top). */
#include "../core/combat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int letter_item(char c) {
    static const char *L = "GcsRBTOLCAME124PYXDQVNto";
    static const int I[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
    const char *p = strchr(L, c);
    return p && c ? I[p - L] : -1;
}

/* arch: built-in ships */
static const char *arch(const char *name) {
    if (!strcmp(name, "laser4"))   /* the old favourite: lasers on the bow, one generator behind each, plates behind */
        return "L3L3L3L3....\nG3G3G3G3....\nB0B0B0B0B0..\n............\n............\n............\n";
    if (!strcmp(name, "rail"))     /* two railguns each on a generator, Titanium in front of the stack, vents ... */
        return "............\nG0R0........\nG0R0........\nT0--------..\n............\n............\n";
    if (!strcmp(name, "cannon"))
        return "............\nG0C0--......\nG0C0--......\nB0B0B0B0B0B0\n............\n............\n";
    return NULL;
}

static int load(const char *spec, Dw2Ship *s) {
    char buf[1024]; const char *txt = arch(spec);
    if (!txt) {
        FILE *f = fopen(spec, "r"); if (!f) { fprintf(stderr, "no ship %s\n", spec); return -1; }
        size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); txt = buf;
    }
    dw2_ship_init(s);
    int row = 0, col = 0;
    for (const char *p = txt; *p && row < 6; ) {
        if (*p == '\n') { row++; col = 0; p++; continue; }
        if (!p[1]) break;
        char a = p[0], b = p[1]; p += 2;
        if (a != '.' && a != '-') {
            int item = letter_item(a);
            if (item < 0) { fprintf(stderr, "bad cell %c%c at %d,%d\n", a, b, row, col); return -1; }
            if (dw2_ship_place(s, item, row, col, b - '0') < 0) fprintf(stderr, "%s: cannot place %s at %d,%d (budget or overlap)\n", spec, dw2_catalog[item].name, row, col);
        }
        col++;
    }
    return 0;
}

static void show(const Dw2Ship *s, const char *title) {
    printf("%s  hull %.0f  shield %.0f  structure %.0f/%.0f  cost %d  weight %d  dodge %.0f%%\n", title, s->hull_pct, s->shield, dw2_ship_structure(s), dw2_ship_structure_max(s), s->cost, s->weight, s->evade_pct);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: dw2_trace A B [-q]\n"); return 2; }
    int quiet = argc > 3 && !strcmp(argv[3], "-q");
    Dw2Ship a, b;
    if (load(argv[1], &a) || load(argv[2], &b)) return 1;
    b.idx = 1;
    dw2_ship_start_combat(&a); dw2_ship_start_combat(&b);
    show(&a, "A"); show(&b, "B");
    int t = 0; Dw2MatchResult r = DW2_RESULT_ONGOING;
    while (r == DW2_RESULT_ONGOING) {
        Dw2EventLog log; memset(&log, 0, sizeof log);
        dw2_match_tick(&a, &b, t, &log); t++;
        if (!quiet) {
            printf("-- second %d:", t);
            for (int i = 0; i < log.n; i++) {
                const Dw2Event *e = &log.ev[i]; const char *who = e->side ? "B" : "A";
                switch (e->type) {
                case DW2_EV_FIRE: printf(" [%s %s@%d,%d lane%d]", e->side ? "B" : "A", dw2_catalog[e->b].name, e->a / 6, e->a % 6, e->c); break;
                case DW2_EV_HIT: printf(" %s(%d,%d)-%d->%d%s", who, e->a / 6, e->a % 6, e->b, e->c, (e->d & DW2_HITF_DESTROYED) ? " DESTROYED" : ""); break;
                case DW2_EV_HULL: printf(" %s-HULL-%d->%d", who, e->a, e->b); break;
                case DW2_EV_EVADE: printf(" %s-DODGED-lane%d", who, e->a); break;
                case DW2_EV_SHIELD: printf(" %s-SHIELD-%d", who, e->a); break;
                case DW2_EV_HEAL: printf(" %s-mend+%.1f", who, e->b / 10.0); break;
                case DW2_EV_REPAIR: printf(" %s(%d,%d)+%d", who, e->a / 6, e->a % 6, e->b); break;
                case DW2_EV_HEAT: printf(" %s(%d,%d)heat-%d%s", who, e->a / 6, e->a % 6, e->b, e->d ? " DESTROYED" : ""); break;
                case DW2_EV_BURN: printf(" %s(%d,%d)BURNT", who, e->a / 6, e->a % 6); break;
                }
            }
            printf("\n");
        }
        r = dw2_match_result(&a, &b, t);
    }
    show(&a, "A end"); show(&b, "B end");
    printf("result after %d s: %s\n", t, r == DW2_RESULT_SELF_WIN ? "A wins" : r == DW2_RESULT_ENEMY_WIN ? "B wins" : "tie");
    return 0;
}
