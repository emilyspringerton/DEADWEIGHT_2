#include "combat.h"
#include <string.h>

void dw2_ship_init(Dw2Ship *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < DW2_MAX_PLACEMENTS; i++) s->placements[i].item_id = -1;
    s->hull_pct = 100.0f;
    s->dmg_mult = 1.0f;
}

int dw2_ship_place(Dw2Ship *s, int item_id, int anchor_row, int anchor_col, int rotation) {
    if (s->placement_n >= DW2_MAX_PLACEMENTS) return -1;
    if (item_id < 0 || item_id >= DW2_ITEM_COUNT) return -1;
    const Dw2ItemDef *def = &dw2_catalog[item_id];
    Dw2PlacedCell cells[DW2_MAX_CUT_CELLS];
    dw2_mask mask = dw2_shape_cells(def->base, def->base_n, rotation, anchor_row, anchor_col, cells);
    for (int i = 0; i < def->base_n; i++) if (!cells[i].in_bounds) return -1;
    if (!dw2_grid_legal(&s->grid, mask)) return -1;
    dw2_grid_place(&s->grid, mask);
    int idx = s->placement_n++;
    Dw2Placement *pl = &s->placements[idx];
    pl->item_id = item_id;
    pl->anchor_row = anchor_row; pl->anchor_col = anchor_col; pl->rotation = rotation;
    pl->cut = 0;
    pl->cell_n = def->base_n;
    for (int i = 0; i < def->base_n; i++) pl->cells[i] = cells[i];
    pl->mask = mask;
    s->weapon_charge[idx] = 0;
    return idx;
}

int dw2_ship_panic_cut(Dw2Ship *s, int placement_idx) {
    if (placement_idx < 0 || placement_idx >= s->placement_n) return 0;
    Dw2Placement *pl = &s->placements[placement_idx];
    if (pl->item_id < 0 || pl->cut) return 0;
    const Dw2ItemDef *def = &dw2_catalog[pl->item_id];
    if (def->split_class == DW2_SPLIT_NONE) return 0;

    Dw2Cell concat[DW2_MAX_CUT_CELLS];
    int n = dw2_concat_fragments(def, concat);
    Dw2PlacedCell cells[DW2_MAX_CUT_CELLS];
    dw2_mask new_mask = dw2_shape_cells(concat, n, pl->rotation, pl->anchor_row, pl->anchor_col, cells);
    for (int i = 0; i < n; i++) if (!cells[i].in_bounds) return 0;

    dw2_mask others_occupied = s->grid.occupied & ~pl->mask;
    if (new_mask & (others_occupied | s->grid.blockade | s->grid.ruined)) return 0;

    dw2_mask vacated = pl->mask & ~new_mask;
    s->grid.ruined |= vacated;
    s->grid.occupied = (s->grid.occupied & ~pl->mask) | new_mask;

    pl->cut = 1;
    pl->cell_n = n;
    for (int i = 0; i < n; i++) pl->cells[i] = cells[i];
    pl->mask = new_mask;

    /* "the waste from a Panic Cut (Dead Squares) becomes armor" -- live bonus, not deferred to a
     * recompute-from-scratch pass, so a mid-fight cut has an immediate, observable second effect
     * beyond severing/rerouting energy. */
    for (int i = 0; i < n; i++) if (cells[i].role == DW2_ROLE_DEAD) s->armor += DW2_ARMOR_PER_DEAD_CELL;
    return 1;
}

void dw2_ship_start_combat(Dw2Ship *s) {
    s->hull_pct = 100.0f;
    s->armor = 0;
    s->cargo_value = 0;
    s->dmg_mult = 1.0f;
    for (int p = 0; p < s->placement_n; p++) {
        if (s->placements[p].item_id < 0) continue;
        s->cargo_value += dw2_catalog[s->placements[p].item_id].value;
        for (int c = 0; c < s->placements[p].cell_n; c++) {
            uint8_t role = s->placements[p].cells[c].role;
            if (role == DW2_ROLE_ARMOR) s->armor += DW2_ARMOR_PER_ARMOR_ITEM;
            else if (role == DW2_ROLE_DEAD) s->armor += DW2_ARMOR_PER_DEAD_CELL;
        }
    }
}

typedef struct { int8_t role; uint8_t port; int8_t placement_idx; } Dw2NetCell;

static int dw2_path_index(const int *path, int path_len, int cell) {
    for (int i = 0; i < path_len; i++) if (path[i] == cell) return i;
    return -1;
}

static void dw2_flow(Dw2Ship *s, Dw2NetCell *net, int cell, uint8_t incoming_dir,
                      int *path, int *path_len, int *activated, dw2_mask *loop_hits);

/* Forwards energy out of `cell` along every direction bit set in `out_ports`, bounds-checked so a
 * grid-edge cell never wraps into the next/previous row (dw2_cell(row, col+-1) is only valid math
 * when col+-1 is still inside [0, DW2_GRID_W)). */
static void dw2_flow_dir(Dw2Ship *s, Dw2NetCell *net, int cell, uint8_t out_ports,
                          int *path, int *path_len, int *activated, dw2_mask *loop_hits) {
    int row = cell / DW2_GRID_W, col = cell % DW2_GRID_W;
    if ((out_ports & DW2_PORT_N) && row - 1 >= 0)
        dw2_flow(s, net, dw2_cell(row - 1, col), DW2_PORT_S, path, path_len, activated, loop_hits);
    if ((out_ports & DW2_PORT_E) && col + 1 < DW2_GRID_W)
        dw2_flow(s, net, dw2_cell(row, col + 1), DW2_PORT_W, path, path_len, activated, loop_hits);
    if ((out_ports & DW2_PORT_S) && row + 1 < DW2_GRID_H)
        dw2_flow(s, net, dw2_cell(row + 1, col), DW2_PORT_N, path, path_len, activated, loop_hits);
    if ((out_ports & DW2_PORT_W) && col - 1 >= 0)
        dw2_flow(s, net, dw2_cell(row, col - 1), DW2_PORT_E, path, path_len, activated, loop_hits);
}

static void dw2_flow(Dw2Ship *s, Dw2NetCell *net, int cell, uint8_t incoming_dir,
                      int *path, int *path_len, int *activated, dw2_mask *loop_hits) {
    if (dw2_bit(cell) & s->burned) return;
    Dw2NetCell *nc = &net[cell];
    if (nc->role < 0) return;                 /* empty cell */
    if (!(nc->port & incoming_dir)) return;    /* not wired to accept from this direction */

    int on_path_idx = dw2_path_index(path, *path_len, cell);
    if (on_path_idx >= 0) {
        /* Closed loop: path[on_path_idx .. path_len-1] plus this cell form the loop
         * (SPEC_REVIEW.md's own Back-EMF meltdown, the sole failure mode for bad wiring). */
        dw2_mask loop_mask = 0;
        for (int i = on_path_idx; i < *path_len; i++) loop_mask |= dw2_bit(path[i]);
        *loop_hits |= loop_mask;
        return;
    }
    if (activated[cell]) return; /* many-to-one: SPEC_REVIEW.md §3 -- inert, not a merge/conflict */
    activated[cell] = 1;
    path[(*path_len)++] = cell;

    switch (nc->role) {
        case DW2_ROLE_WEAPON:
            s->weapon_charge[nc->placement_idx] += DW2_GENERATOR_OUTPUT;
            break;
        case DW2_ROLE_CONDUCTOR:
        case DW2_ROLE_SPLITTER: {
            /* Forward out every active port except the one that just received (`incoming_dir` is
             * this cell's OWN port bit, not the sender's) -- excluding anything else would send
             * energy right back out the port it just arrived on, straight back to the sender. */
            uint8_t out_ports = nc->port & (uint8_t)~incoming_dir;
            dw2_flow_dir(s, net, cell, out_ports, path, path_len, activated, loop_hits);
            break;
        }
        case DW2_ROLE_ARMOR:
        case DW2_ROLE_NONE:
        case DW2_ROLE_DEAD:
        case DW2_ROLE_GENERATOR:
        default:
            break; /* sink / inert for routing purposes */
    }
    (*path_len)--;
}

static void dw2_flow_from_generator(Dw2Ship *s, Dw2NetCell *net, int gcell, int *activated, dw2_mask *loop_hits) {
    if (dw2_bit(gcell) & s->burned) return;
    int path[DW2_GRID_CELLS + 4];
    int path_len = 0;
    path[path_len++] = gcell;
    activated[gcell] = 1;
    dw2_flow_dir(s, net, gcell, net[gcell].port, path, &path_len, activated, loop_hits);
}

static void dw2_ship_apply_damage(Dw2Ship *s, float dmg) {
    if (s->armor > 0) {
        float absorbed = dmg < s->armor ? dmg : s->armor;
        s->armor -= absorbed;
        dmg -= absorbed;
    }
    if (dmg > 0) {
        s->hull_pct -= dmg;
        if (s->hull_pct < 0) s->hull_pct = 0;
    }
}

/* V0 simplification, named not hidden: `loop_hits` ORs together every closed loop found in a
 * single tick into one mask and tracks it as a single region (one Dw2LoopTracker), rather than
 * independently tracking multiple simultaneous distinct bad loops. A real, deliberate scope cut
 * for the first playable prototype (PHASE_D1_CORE_LOOP.md's own bar is "a deliberately-built
 * closed loop triggers meltdown," not "arbitrarily many independent loops are perfectly
 * accounted"), not an oversight -- revisit only if playtesting finds multi-loop builds common. */
void dw2_ship_tick(Dw2Ship *self, Dw2Ship *enemy) {
    Dw2NetCell net[DW2_GRID_CELLS];
    for (int i = 0; i < DW2_GRID_CELLS; i++) { net[i].role = -1; net[i].port = 0; net[i].placement_idx = -1; }
    for (int p = 0; p < self->placement_n; p++) {
        Dw2Placement *pl = &self->placements[p];
        if (pl->item_id < 0) continue;
        for (int c = 0; c < pl->cell_n; c++) {
            Dw2PlacedCell *pc = &pl->cells[c];
            if (!pc->in_bounds) continue;
            int idx = dw2_cell(pc->row, pc->col);
            net[idx].role = (int8_t)pc->role;
            net[idx].port = pc->port;
            net[idx].placement_idx = (int8_t)p;
        }
    }

    int activated[DW2_GRID_CELLS];
    memset(activated, 0, sizeof activated);
    dw2_mask loop_hits = 0;
    for (int i = 0; i < DW2_GRID_CELLS; i++)
        if (net[i].role == DW2_ROLE_GENERATOR && !(dw2_bit(i) & self->burned))
            dw2_flow_from_generator(self, net, i, activated, &loop_hits);

    if (loop_hits) {
        if (!self->loop.active || self->loop.cells != loop_hits) {
            self->loop.active = 1;
            self->loop.cells = loop_hits;
            self->loop.phi = DW2_GENERATOR_OUTPUT;
        }
        self->loop.phi *= DW2_BACK_EMF_GROWTH;
        if (self->loop.phi > DW2_BACK_EMF_SHATTER_MULT * DW2_GENERATOR_OUTPUT) {
            self->burned |= self->loop.cells;
            self->loop.active = 0;
            self->shatter_events++;
        }
    } else {
        self->loop.active = 0;
    }

    for (int p = 0; p < self->placement_n; p++) {
        if (self->placements[p].item_id < 0) continue;
        if (self->weapon_charge[p] >= DW2_WEAPON_CHARGE_THRESHOLD) {
            self->weapon_charge[p] -= DW2_WEAPON_CHARGE_THRESHOLD;
            /* dmg_mult (core/round.h): a graded Overcharge round-break call scales this ship's own
             * outgoing damage for the current round; 1.0 (the default) is byte-identical to pre-
             * round-break behavior. */
            dw2_ship_apply_damage(enemy, DW2_WEAPON_DAMAGE * self->dmg_mult);
        }
    }
}

Dw2MatchResult dw2_match_result(const Dw2Ship *self, const Dw2Ship *enemy, int elapsed_ticks) {
    int self_dead = self->hull_pct <= 0.0f;
    int enemy_dead = enemy->hull_pct <= 0.0f;
    if (self_dead && enemy_dead) return DW2_RESULT_TIE;
    if (enemy_dead) return DW2_RESULT_SELF_WIN;
    if (self_dead) return DW2_RESULT_ENEMY_WIN;
    if (elapsed_ticks < DW2_MATCH_TIMEOUT_TICKS) return DW2_RESULT_ONGOING;
    /* SPEC_REVIEW.md §2: remaining hull% is the tiebreak, cargo value the secondary tiebreak. */
    if (self->hull_pct > enemy->hull_pct) return DW2_RESULT_SELF_WIN;
    if (enemy->hull_pct > self->hull_pct) return DW2_RESULT_ENEMY_WIN;
    if (self->cargo_value > enemy->cargo_value) return DW2_RESULT_SELF_WIN;
    if (enemy->cargo_value > self->cargo_value) return DW2_RESULT_ENEMY_WIN;
    return DW2_RESULT_TIE;
}
