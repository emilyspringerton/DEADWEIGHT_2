#include "combat.h"
#include "cannon.h"
#include <string.h>

static float dw2_ore_armor(const Dw2Ship *s);

Dw2WeaponStats dw2_weapon_stats(int item_id) {
    Dw2WeaponStats w = { 0.0f, 0.0f, 0 };
    switch (item_id) {
    case DW2_ITEM_RAILGUN:      w.threshold = DW2_WEAPON_CHARGE_THRESHOLD; w.damage = DW2_WEAPON_DAMAGE; break;
    case DW2_ITEM_PULSE_LASER:  w.threshold = 10.0f; w.damage = 3.0f; w.pierce = 1; break;
    case DW2_ITEM_SIEGE_CANNON: w.threshold = 50.0f; w.damage = 30.0f; break;
    default: break;
    }
    return w;
}

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
    for (int i = 0; i < n; i++) if (cells[i].role == DW2_ROLE_DEAD) { s->armor += DW2_ARMOR_PER_DEAD_CELL; s->armor_max += DW2_ARMOR_PER_DEAD_CELL; }
    /* a cut piece is no longer intact: cutting Aether Ore takes back the armor the Bulwark cells touching it were worth */
    float ore = dw2_ore_armor(s), d = ore - s->ore_armor;
    s->ore_armor = ore;
    if (d != 0.0f) { s->armor += d; if (s->armor < 0.0f) s->armor = 0.0f; s->armor_max += d; if (s->armor_max < 0.0f) s->armor_max = 0.0f; }
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
    s->ore_armor = dw2_ore_armor(s);
    s->armor += s->ore_armor;
    s->armor_max = s->armor;
    for (int i = 0; i < DW2_GRID_CELLS; i++) { s->cell_q[i] = 0.0f; s->cell_q2[i] = 0.0f; s->gate_open[i] = 0; }
    for (int p = 0; p < s->placement_n; p++) {
        const Dw2Placement *pl = &s->placements[p];
        if (pl->item_id == DW2_ITEM_BATTERY && pl->cell_n > 0 && pl->cells[0].in_bounds)
            s->cell_q[dw2_cell(pl->cells[0].row, pl->cells[0].col)] = DW2_BAT_START;
    }
}

typedef struct { int8_t role; uint8_t port; int8_t placement_idx; } Dw2NetCell;

/* the placement that owns each cell, -1 where nothing is */
static void dw2_owner_map(const Dw2Ship *s, int8_t *owner) {
    for (int i = 0; i < DW2_GRID_CELLS; i++) owner[i] = -1;
    for (int p = 0; p < s->placement_n; p++) {
        const Dw2Placement *pl = &s->placements[p];
        if (pl->item_id < 0) continue;
        for (int c = 0; c < pl->cell_n; c++)
            if (pl->cells[c].in_bounds) owner[dw2_cell(pl->cells[c].row, pl->cells[c].col)] = (int8_t)p;
    }
}

/* Does the cell touch (share an edge with) a cell of an intact, uncut placement of `item_id`? Position is rules: a Titanium Beam
 * boosts the weapons touching it, Aether Ore the defenses, and cutting either one loses that. */
static int dw2_cell_touches(const Dw2Ship *s, const int8_t *owner, int cell, int item_id) {
    int row = cell / DW2_GRID_W, col = cell % DW2_GRID_W;
    int nb[4], n = 0;
    if (row > 0) nb[n++] = cell - DW2_GRID_W;
    if (col + 1 < DW2_GRID_W) nb[n++] = cell + 1;
    if (row + 1 < DW2_GRID_H) nb[n++] = cell + DW2_GRID_W;
    if (col > 0) nb[n++] = cell - 1;
    for (int i = 0; i < n; i++) {
        int o = owner[nb[i]];
        if (o >= 0 && o != owner[cell] && s->placements[o].item_id == item_id && !s->placements[o].cut) return 1;
    }
    return 0;
}

static int dw2_placement_touches(const Dw2Ship *s, const int8_t *owner, int p, int item_id) {
    const Dw2Placement *pl = &s->placements[p];
    for (int c = 0; c < pl->cell_n; c++)
        if (pl->cells[c].in_bounds && dw2_cell_touches(s, owner, dw2_cell(pl->cells[c].row, pl->cells[c].col), item_id)) return 1;
    return 0;
}

/* the armor the Bulwark cells that touch intact Aether Ore are worth on top of their own */
static float dw2_ore_armor(const Dw2Ship *s) {
    int8_t owner[DW2_GRID_CELLS];
    dw2_owner_map(s, owner);
    float a = 0.0f;
    for (int p = 0; p < s->placement_n; p++) {
        const Dw2Placement *pl = &s->placements[p];
        if (pl->item_id < 0) continue;
        for (int c = 0; c < pl->cell_n; c++)
            if (pl->cells[c].in_bounds && pl->cells[c].role == DW2_ROLE_ARMOR &&
                dw2_cell_touches(s, owner, dw2_cell(pl->cells[c].row, pl->cells[c].col), DW2_ITEM_AETHER_ORE)) a += DW2_ORE_ARMOR_BONUS;
    }
    return a;
}

float dw2_resistor_factor(int item_id) {
    return item_id == DW2_ITEM_RESISTOR_1 ? 0.75f : item_id == DW2_ITEM_RESISTOR_2 ? 0.5f : item_id == DW2_ITEM_RESISTOR_4 ? 0.25f : 1.0f;
}

uint8_t dw2_rotated_port(uint8_t port, int rotation) {
    rotation = ((rotation % 4) + 4) % 4;
    for (int r = 0; r < rotation; r++) port = dw2_rotate_port_cw(port);
    return port;
}

static int dw2_popcount4(uint8_t m) { return ((m >> 3) & 1) + ((m >> 2) & 1) + ((m >> 1) & 1) + (m & 1); }

/* One second of power moving through a ship. Power is in watts (a Generator makes 10). It leaves a part by every open port but the one it
 * came in by, DIVIDED equally between them; every route that reaches a cell adds its share, so two feeds into one wire add up. A stateful part
 * (Capacitor, Battery, Delayer) ends the walk: what reaches it is banked and shows up as its output on a LATER tick, which is what makes timing
 * a skill. Reaching a cell already on the current route is a closed loop (Back-EMF). */
typedef struct {
    Dw2Ship *s;
    Dw2NetCell net[DW2_GRID_CELLS];
    float pin[DW2_GRID_CELLS];        /* power delivered to the cell this tick */
    float absorb[DW2_GRID_CELLS];     /* ... of which a stateful part banks */
    float gate_in[DW2_GRID_CELLS];    /* ... or a Transistor's gate sees */
    int path[DW2_GRID_CELLS + 4];
    int path_len;
    int budget;
    dw2_mask loop_hits;
} Dw2Flow;

static void dw2_deliver(Dw2Flow *f, int cell, uint8_t incoming, float power);

static void dw2_forward(Dw2Flow *f, int cell, uint8_t out_ports, float power) {
    int n = dw2_popcount4(out_ports);
    if (n == 0) return;
    float share = power / (float)n;
    int row = cell / DW2_GRID_W, col = cell % DW2_GRID_W;
    if ((out_ports & DW2_PORT_N) && row - 1 >= 0) dw2_deliver(f, dw2_cell(row - 1, col), DW2_PORT_S, share);
    if ((out_ports & DW2_PORT_E) && col + 1 < DW2_GRID_W) dw2_deliver(f, dw2_cell(row, col + 1), DW2_PORT_W, share);
    if ((out_ports & DW2_PORT_S) && row + 1 < DW2_GRID_H) dw2_deliver(f, dw2_cell(row + 1, col), DW2_PORT_N, share);
    if ((out_ports & DW2_PORT_W) && col - 1 >= 0) dw2_deliver(f, dw2_cell(row, col - 1), DW2_PORT_E, share);
}

static void dw2_deliver(Dw2Flow *f, int cell, uint8_t incoming, float power) {
    Dw2Ship *s = f->s;
    if (power < 0.001f) return;
    if (dw2_bit(cell) & s->burned) return;
    Dw2NetCell *nc = &f->net[cell];
    if (nc->role < 0) return;                 /* empty cell */
    if (!(nc->port & incoming)) return;        /* not wired to accept from this direction */
    int rot = s->placements[nc->placement_idx].rotation;
    uint8_t in_port = dw2_rotated_port(DW2_PORT_W, rot), gate_port = dw2_rotated_port(DW2_PORT_N, rot);
    if ((nc->role == DW2_ROLE_CAPACITOR || nc->role == DW2_ROLE_BATTERY || nc->role == DW2_ROLE_DELAYER) && incoming != in_port) return;
    if (nc->role == DW2_ROLE_TRANSISTOR && incoming != in_port && incoming != gate_port) return;
    for (int i = 0; i < f->path_len; i++) {
        if (f->path[i] == cell) {
            /* Closed loop: path[i .. path_len-1] plus this cell (SPEC_REVIEW.md's Back-EMF meltdown, the failure mode for bad wiring). */
            for (int k = i; k < f->path_len; k++) f->loop_hits |= dw2_bit(f->path[k]);
            return;
        }
    }
    if (f->budget <= 0) return;
    f->budget--;
    if (power > DW2_POWER_MAX) power = DW2_POWER_MAX;
    f->pin[cell] += power;
    f->path[f->path_len++] = cell;
    uint8_t out = nc->port & (uint8_t)~incoming;   /* `incoming` is this cell's OWN port bit, not the sender's */
    switch (nc->role) {
        case DW2_ROLE_WEAPON:
            s->weapon_charge[nc->placement_idx] += power;
            break;
        case DW2_ROLE_AMPLIFIER:
            power *= DW2_AMPLIFIER_GAIN;
            dw2_forward(f, cell, out, power);
            break;
        case DW2_ROLE_CONDUCTOR:
        case DW2_ROLE_SPLITTER:
            dw2_forward(f, cell, out, power);
            break;
        case DW2_ROLE_RESISTOR:
            power *= dw2_resistor_factor(s->placements[nc->placement_idx].item_id);
            dw2_forward(f, cell, out, power);
            break;
        case DW2_ROLE_CAPACITOR:
        case DW2_ROLE_BATTERY:
        case DW2_ROLE_DELAYER:
            f->absorb[cell] += power;
            break;
        case DW2_ROLE_TRANSISTOR:
            if (incoming == gate_port) f->gate_in[cell] += power;
            else if (s->gate_open[cell]) dw2_forward(f, cell, (uint8_t)(nc->port & ~in_port & ~gate_port), power);
            break;
        default:
            break; /* Mender / Aegis (read from pin), Armor, Dead, Generator, cargo: a sink */
    }
    f->path_len--;
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

static void dw2_ship_apply_pierce(Dw2Ship *s, float dmg) {
    s->hull_pct -= dmg;
    if (s->hull_pct < 0) s->hull_pct = 0;
}

/* V0 simplification, named not hidden: `loop_hits` ORs together every closed loop found in a
 * single tick into one mask and tracks it as a single region (one Dw2LoopTracker), rather than
 * independently tracking multiple simultaneous distinct bad loops. A real, deliberate scope cut
 * for the first playable prototype (PHASE_D1_CORE_LOOP.md's own bar is "a deliberately-built
 * closed loop triggers meltdown," not "arbitrarily many independent loops are perfectly
 * accounted"), not an oversight -- revisit only if playtesting finds multi-loop builds common. */
void dw2_ship_tick(Dw2Ship *self, Dw2Ship *enemy) {
    Dw2Flow f;
    memset(&f, 0, sizeof f);
    f.s = self; f.budget = DW2_FLOW_BUDGET;
    for (int i = 0; i < DW2_GRID_CELLS; i++) { f.net[i].role = -1; f.net[i].port = 0; f.net[i].placement_idx = -1; }
    for (int p = 0; p < self->placement_n; p++) {
        Dw2Placement *pl = &self->placements[p];
        if (pl->item_id < 0) continue;
        for (int c = 0; c < pl->cell_n; c++) {
            Dw2PlacedCell *pc = &pl->cells[c];
            if (!pc->in_bounds) continue;
            int idx = dw2_cell(pc->row, pc->col);
            f.net[idx].role = (int8_t)pc->role;
            f.net[idx].port = pc->port;
            f.net[idx].placement_idx = (int8_t)p;
        }
    }

    /* the sources, in cell order: a Generator makes 10; a Battery gives up to 15 of what it holds; a full Capacitor lets go of everything;
     * a Delayer lets go of what it was fed two ticks ago. A part that stores power puts it out of its output side. */
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        if (dw2_bit(i) & self->burned) continue;
        int8_t role = f.net[i].role;
        uint8_t ports = f.net[i].port;
        uint8_t out = f.net[i].placement_idx >= 0 ? dw2_rotated_port(DW2_PORT_E, self->placements[f.net[i].placement_idx].rotation) : 0;
        float power = 0.0f;
        if (role == DW2_ROLE_GENERATOR) {
            f.path[0] = i; f.path_len = 1; f.pin[i] = DW2_GENERATOR_OUTPUT;
            dw2_forward(&f, i, ports, DW2_GENERATOR_OUTPUT);
            f.path_len = 0;
            continue;
        } else if (role == DW2_ROLE_BATTERY) {
            power = self->cell_q[i] < DW2_BAT_RATE ? self->cell_q[i] : DW2_BAT_RATE;
            self->cell_q[i] -= power;
        } else if (role == DW2_ROLE_CAPACITOR) {
            if (self->cell_q[i] >= DW2_CAP_FULL) { power = self->cell_q[i]; self->cell_q[i] = 0.0f; }
        } else if (role == DW2_ROLE_DELAYER) {
            power = self->cell_q2[i];
            self->cell_q2[i] = self->cell_q[i];
            self->cell_q[i] = 0.0f;
        }
        if (power >= 0.001f) { f.path_len = 0; dw2_forward(&f, i, out, power); }
    }
    dw2_mask loop_hits = f.loop_hits;

    /* what the stateful parts were fed this tick is banked (it will matter on a later one) */
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        int8_t role = f.net[i].role;
        if (role == DW2_ROLE_BATTERY) {
            self->cell_q[i] += f.absorb[i];
            if (self->cell_q[i] > DW2_BAT_MAX) self->cell_q[i] = DW2_BAT_MAX;
        } else if (role == DW2_ROLE_CAPACITOR) {
            self->cell_q[i] += f.absorb[i];
            if (self->cell_q[i] > DW2_CAP_MAX) self->cell_q[i] = DW2_CAP_MAX;
        } else if (role == DW2_ROLE_DELAYER) {
            self->cell_q[i] += f.absorb[i];
        } else if (role == DW2_ROLE_TRANSISTOR) {
            self->gate_open[i] = f.gate_in[i] >= DW2_GATE_MIN;
        }
    }

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

    int8_t owner[DW2_GRID_CELLS];
    dw2_owner_map(self, owner);
    for (int p = 0; p < self->placement_n; p++) {
        if (self->placements[p].item_id < 0) continue;
        Dw2WeaponStats ws = dw2_weapon_stats(self->placements[p].item_id);
        if (ws.threshold <= 0.0f) continue;
        float boost = dw2_placement_touches(self, owner, p, DW2_ITEM_TITANIUM_BEAM) ? DW2_BEAM_BOOST : 1.0f;
        /* a weapon fed more than its threshold in one second shoots more than once */
        while (self->weapon_charge[p] >= ws.threshold) {
            float shot_dmg = ws.damage * self->dmg_mult * boost;
            /* "Cannon programming" (EMILY/BACKLOG.md SECTION 549, docs/LO_CANNON_PROGRAMMING.md): the fire/hold call is a real, compiled LO
             * program (core/cannon.h). The shipped decision program is FIRE in all 4 states; a HOLD leaves weapon_charge untouched. */
            int self_behind = self->hull_pct < enemy->hull_pct;
            int shot_is_overkill = enemy->hull_pct <= shot_dmg;
            if (dw2_cannon_decide(self_behind, shot_is_overkill) == DW2_CANNON_HOLD) break;
            self->weapon_charge[p] -= ws.threshold;
            if (ws.pierce) dw2_ship_apply_pierce(enemy, shot_dmg);
            else dw2_ship_apply_damage(enemy, shot_dmg);
        }
    }

    /* the support parts: a Mender mends 2 hull per 10 W it is fed, an Aegis Emitter refills 3 armor per 10 W (x1.5 touching Aether Ore) */
    if (self->hull_pct > 0.0f) {
        for (int i = 0; i < DW2_GRID_CELLS; i++) {
            if (!(f.pin[i] > 0.0f) || (dw2_bit(i) & self->burned)) continue;
            if (f.net[i].role == DW2_ROLE_MENDER) {
                self->hull_pct += DW2_MENDER_HEAL * (f.pin[i] / DW2_GENERATOR_OUTPUT);
                if (self->hull_pct > 100.0f) self->hull_pct = 100.0f;
            } else if (f.net[i].role == DW2_ROLE_AEGIS && self->armor < self->armor_max) {
                float r = DW2_AEGIS_RECHARGE * (f.pin[i] / DW2_GENERATOR_OUTPUT);
                if (dw2_cell_touches(self, owner, i, DW2_ITEM_AETHER_ORE)) r *= DW2_ORE_AEGIS_BOOST;
                self->armor += r;
                if (self->armor > self->armor_max) self->armor = self->armor_max;
            }
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
