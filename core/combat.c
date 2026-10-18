#include "combat.h"
#include "cannon.h"
#include <string.h>

Dw2WeaponStats dw2_weapon_stats(int item_id) {
    Dw2WeaponStats w = { 0.0f, 0.0f, DW2_SHOT_NONE };
    switch (item_id) {
    case DW2_ITEM_RAILGUN:      w.threshold = 30.0f; w.damage = 30.0f; w.kind = DW2_SHOT_RAIL; break;
    case DW2_ITEM_PULSE_LASER:  w.threshold = 10.0f; w.damage = 7.0f;  w.kind = DW2_SHOT_LASER; break;
    case DW2_ITEM_SIEGE_CANNON: w.threshold = 30.0f; w.damage = 45.0f; w.kind = DW2_SHOT_CANNON; break;
    default: break;
    }
    return w;
}

float dw2_resistor_factor(int item_id) {
    return item_id == DW2_ITEM_RESISTOR_1 ? 0.75f : item_id == DW2_ITEM_RESISTOR_2 ? 0.5f : item_id == DW2_ITEM_RESISTOR_4 ? 0.25f : 1.0f;
}

uint8_t dw2_rotated_port(uint8_t port, int rotation) {
    rotation = ((rotation % 4) + 4) % 4;
    for (int r = 0; r < rotation; r++) port = dw2_rotate_port_cw(port);
    return port;
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
    if (s->cost + def->cost > DW2_BUDGET_CREDITS || s->weight + def->weight > DW2_BUDGET_WEIGHT) return -1;
    Dw2PlacedCell cells[DW2_MAX_ITEM_CELLS];
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
    s->cost += def->cost; s->weight += def->weight;
    return idx;
}

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

void dw2_ship_start_combat(Dw2Ship *s) {
    s->hull_pct = 100.0f;
    s->shield = 0;
    s->cargo_value = 0;
    s->dmg_mult = 1.0f;
    s->dead = 0; s->burned = 0;
    for (int i = 0; i < DW2_GRID_CELLS; i++) { s->cell_hp[i] = 0; s->cell_max[i] = 0; s->pin[i] = 0; }
    for (int p = 0; p < s->placement_n; p++) {
        if (s->placements[p].item_id < 0) continue;
        const Dw2ItemDef *d = &dw2_catalog[s->placements[p].item_id];
        s->cargo_value += d->cost;
        for (int c = 0; c < s->placements[p].cell_n; c++) {
            const Dw2PlacedCell *pc = &s->placements[p].cells[c];
            if (!pc->in_bounds) continue;
            int cell = dw2_cell(pc->row, pc->col);
            s->cell_hp[cell] = s->cell_max[cell] = (float)d->hp;
        }
        s->weapon_charge[p] = 0; s->shots[p] = 0; s->shot_dmg[p] = 0;
    }
    s->evade_pct = (float)(DW2_EVADE_REF_WEIGHT - s->weight) * DW2_EVADE_PER_TON;
    if (s->evade_pct < 0.0f) s->evade_pct = 0.0f;
    if (s->evade_pct > DW2_EVADE_MAX) s->evade_pct = DW2_EVADE_MAX;
    s->evade_acc = 0.0f;
    for (int i = 0; i < DW2_GRID_CELLS; i++) { s->cell_q[i] = 0.0f; s->cell_q2[i] = 0.0f; s->gate_open[i] = 0; }
    for (int p = 0; p < s->placement_n; p++) {
        const Dw2Placement *pl = &s->placements[p];
        if (pl->item_id == DW2_ITEM_BATTERY && pl->cell_n > 0 && pl->cells[0].in_bounds)
            s->cell_q[dw2_cell(pl->cells[0].row, pl->cells[0].col)] = DW2_BAT_START;
    }
}

/* ---- readers ---- */
int dw2_cell_alive(const Dw2Ship *s, int cell) { return s->cell_max[cell] > 0.0f && !(s->dead & dw2_bit(cell)); }

float dw2_ship_structure(const Dw2Ship *s) {
    float t = 0.0f;
    for (int i = 0; i < DW2_GRID_CELLS; i++) if (dw2_cell_alive(s, i)) t += s->cell_hp[i];
    return t;
}
float dw2_ship_structure_max(const Dw2Ship *s) {
    float t = 0.0f;
    for (int i = 0; i < DW2_GRID_CELLS; i++) t += s->cell_max[i];
    return t;
}
float dw2_ship_standing_score(const Dw2Ship *s) { return s->hull_pct + s->shield + dw2_ship_structure(s) / 8.0f; }

static const Dw2ItemDef *dw2_cell_def(const Dw2Ship *s, const int8_t *owner, int cell) {
    return owner[cell] >= 0 ? &dw2_catalog[s->placements[owner[cell]].item_id] : NULL;
}

float dw2_cell_mult(const Dw2Ship *s, int cell, int kind) {
    int8_t owner[DW2_GRID_CELLS]; dw2_owner_map(s, owner);
    const Dw2ItemDef *d = dw2_cell_def(s, owner, cell);
    if (!d) return 1.0f;
    return kind == DW2_SHOT_LASER ? d->x_laser : kind == DW2_SHOT_RAIL ? d->x_rail : d->x_cannon;
}

int dw2_lane_clear_ahead(const Dw2Ship *s, int cell) {
    int row = cell / DW2_GRID_W, col = cell % DW2_GRID_W;
    for (int r = 0; r < row; r++) if (dw2_cell_alive(s, dw2_cell(r, col))) return 0;
    return 1;
}

static int dw2_nb(int cell, int k) {      /* the k-th edge neighbour (N E S W), or -1 off the grid */
    int row = cell / DW2_GRID_W, col = cell % DW2_GRID_W;
    switch (k) {
    case 0: return row > 0 ? cell - DW2_GRID_W : -1;
    case 1: return col + 1 < DW2_GRID_W ? cell + 1 : -1;
    case 2: return row + 1 < DW2_GRID_H ? cell + DW2_GRID_W : -1;
    default: return col > 0 ? cell - 1 : -1;
    }
}

static int dw2_is_role(const Dw2Ship *s, const int8_t *owner, int cell, int role) {
    if (owner[cell] < 0 || !dw2_cell_alive(s, cell)) return 0;
    const Dw2Placement *pl = &s->placements[owner[cell]];
    for (int c = 0; c < pl->cell_n; c++)
        if (pl->cells[c].in_bounds && dw2_cell(pl->cells[c].row, pl->cells[c].col) == cell) return pl->cells[c].role == role;
    return 0;
}

int dw2_cell_cooled(const Dw2Ship *s, int cell) {
    int8_t owner[DW2_GRID_CELLS]; dw2_owner_map(s, owner);
    if (dw2_is_role(s, owner, cell, DW2_ROLE_COOLANT)) return 1;
    for (int k = 0; k < 4; k++) { int n = dw2_nb(cell, k); if (n >= 0 && dw2_is_role(s, owner, n, DW2_ROLE_COOLANT)) return 1; }
    return 0;
}

static int dw2_heat_at(const Dw2Ship *s, const int8_t *owner, int cell) {
    if (owner[cell] < 0 || !dw2_cell_alive(s, cell)) return 0;
    int heat = 0;
    for (int k = 0; k < 4; k++) {
        int n = dw2_nb(cell, k);
        if (n < 0 || owner[n] < 0 || owner[n] == owner[cell] || !dw2_cell_alive(s, n)) continue;
        heat += dw2_catalog[s->placements[owner[n]].item_id].heat;
    }
    if (!heat) return 0;
    if (dw2_is_role(s, owner, cell, DW2_ROLE_COOLANT)) return 0;
    for (int k = 0; k < 4; k++) { int n = dw2_nb(cell, k); if (n >= 0 && dw2_is_role(s, owner, n, DW2_ROLE_COOLANT)) return 0; }
    return heat;
}
int dw2_cell_heat(const Dw2Ship *s, int cell) {
    int8_t owner[DW2_GRID_CELLS]; dw2_owner_map(s, owner);
    return dw2_heat_at(s, owner, cell);
}

/* the strongest family boost among the alive cells of OTHER parts that touch any cell of the placement */
static float dw2_touch_boost(const Dw2Ship *s, const int8_t *owner, int placement, int family) {
    const Dw2Placement *pl = &s->placements[placement];
    float best = 1.0f;
    for (int c = 0; c < pl->cell_n; c++) {
        if (!pl->cells[c].in_bounds) continue;
        int cell = dw2_cell(pl->cells[c].row, pl->cells[c].col);
        for (int k = 0; k < 4; k++) {
            int n = dw2_nb(cell, k);
            if (n < 0 || owner[n] < 0 || owner[n] == placement || !dw2_cell_alive(s, n)) continue;
            const Dw2ItemDef *d = &dw2_catalog[s->placements[owner[n]].item_id];
            if (d->family == family && d->boost > best) best = d->boost;
        }
    }
    return best;
}
float dw2_weapon_boost(const Dw2Ship *s, int placement) {
    int8_t owner[DW2_GRID_CELLS]; dw2_owner_map(s, owner);
    return dw2_touch_boost(s, owner, placement, DW2_FAM_TITANIUM);
}
float dw2_aegis_boost(const Dw2Ship *s, int cell) {
    int8_t owner[DW2_GRID_CELLS]; dw2_owner_map(s, owner);
    if (owner[cell] < 0) return 1.0f;
    return dw2_touch_boost(s, owner, owner[cell], DW2_FAM_AETHER);
}

/* ---- the event log ---- */
static void dw2_ev(Dw2EventLog *log, int type, int side, int a, int b, int c, int d, int e) {
    if (!log) return;
    if (log->n >= DW2_EVENT_MAX) { log->dropped++; return; }
    Dw2Event *x = &log->ev[log->n++];
    #define CL(v) ((uint8_t)((v) < 0 ? 0 : (v) > 255 ? 255 : (v)))
    x->type = (uint8_t)type; x->side = (uint8_t)side; x->a = CL(a); x->b = CL(b); x->c = CL(c); x->d = CL(d); x->e = CL(e);
    #undef CL
}
static int dw2_round(float v) { return (int)(v + 0.5f); }

/* ---- power: the circuit ---- */
typedef struct { int8_t role; uint8_t port; int8_t placement_idx; } Dw2NetCell;

static int dw2_popcount4(uint8_t m) { return ((m >> 3) & 1) + ((m >> 2) & 1) + ((m >> 1) & 1) + (m & 1); }

/* One second of power moving through a ship. Power is in watts (a Generator makes 10). It leaves a part by every open port but the one it came in by, DIVIDED equally
 * between them; every route that reaches a cell adds its share, so two feeds into one wire add up. A stateful part (Capacitor, Battery, Delayer) ends the walk: what
 * reaches it is banked and shows up as its output on a LATER tick, which is what makes timing a skill. Reaching a cell already on the current route is a closed loop
 * (Back-EMF). A destroyed cell is not on the board. */
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
    Dw2NetCell *nc = &f->net[cell];
    if (nc->role < 0) return;                 /* empty or destroyed cell */
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
            break; /* Mender / Aegis (read from pin), Armor, Biomass, Coolant, Generator, bodies: a sink */
    }
    f->path_len--;
}

/* A weapon is "whole" when none of its cells is destroyed (a Siege Cannon with its barrel shot off is dead). */
static int dw2_placement_whole(const Dw2Ship *s, int p) {
    const Dw2Placement *pl = &s->placements[p];
    for (int c = 0; c < pl->cell_n; c++)
        if (pl->cells[c].in_bounds && !dw2_cell_alive(s, dw2_cell(pl->cells[c].row, pl->cells[c].col))) return 0;
    return 1;
}

/* the cell of a placement that is its weapon cell (the one fed, whose column is the lane) */
static int dw2_weapon_cell(const Dw2Ship *s, int p) {
    const Dw2Placement *pl = &s->placements[p];
    for (int c = 0; c < pl->cell_n; c++)
        if (pl->cells[c].in_bounds && pl->cells[c].role == DW2_ROLE_WEAPON) return dw2_cell(pl->cells[c].row, pl->cells[c].col);
    return dw2_cell(pl->cells[0].row, pl->cells[0].col);
}

/* Phase 1 for one ship: the circuit runs, stateful parts bank, Back-EMF grows, and every weapon that reached its threshold decides to fire (the volley). */
static void dw2_ship_power(Dw2Ship *self, const Dw2Ship *enemy, Dw2EventLog *log) {
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
            if (self->dead & dw2_bit(idx)) continue;
            f.net[idx].role = (int8_t)pc->role;
            f.net[idx].port = pc->port;
            f.net[idx].placement_idx = (int8_t)p;
        }
    }

    /* the sources, in cell order: a Generator makes 10; a Battery gives up to 15 of what it holds; a full Capacitor lets go of everything;
     * a Delayer lets go of what it was fed two ticks ago. A part that stores power puts it out of its output side. */
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        int8_t role = f.net[i].role;
        if (role < 0) continue;
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
    memcpy(self->pin, f.pin, sizeof self->pin);

    /* the volley: every weapon that reached its threshold, if it is whole and (a laser) has a clear lane */
    int8_t owner[DW2_GRID_CELLS];
    dw2_owner_map(self, owner);
    for (int p = 0; p < self->placement_n; p++) {
        self->shots[p] = 0; self->shot_dmg[p] = 0.0f;
        if (self->placements[p].item_id < 0) continue;
        Dw2WeaponStats ws = dw2_weapon_stats(self->placements[p].item_id);
        if (ws.threshold <= 0.0f) continue;
        int wc = dw2_weapon_cell(self, p);
        int can = dw2_placement_whole(self, p) && (ws.kind != DW2_SHOT_LASER || dw2_lane_clear_ahead(self, wc));
        if (!can) {
            if (self->weapon_charge[p] > ws.threshold) self->weapon_charge[p] = ws.threshold;   /* a gun that cannot fire holds its charge, ready */
            continue;
        }
        float boost = dw2_touch_boost(self, owner, p, DW2_FAM_TITANIUM);
        /* a weapon fed more than its threshold in one second shoots more than once */
        while (self->weapon_charge[p] >= ws.threshold && self->shots[p] < DW2_MAX_SHOTS_PER_TICK) {
            float shot_dmg = ws.damage * self->dmg_mult * boost;
            /* "Cannon programming" (EMILY/BACKLOG.md SECTION 549, docs/LO_CANNON_PROGRAMMING.md): the fire/hold call is a real, compiled LO
             * program (core/cannon.h). The shipped decision program is FIRE in all 4 states; a HOLD leaves weapon_charge untouched. */
            int self_behind = self->hull_pct < enemy->hull_pct;
            int shot_is_overkill = enemy->hull_pct <= shot_dmg;
            if (dw2_cannon_decide(self_behind, shot_is_overkill) == DW2_CANNON_HOLD) break;
            self->weapon_charge[p] -= ws.threshold;
            self->shots[p]++;
            self->shot_dmg[p] = shot_dmg;
        }
        if (self->weapon_charge[p] > ws.threshold * 2.0f) self->weapon_charge[p] = ws.threshold * 2.0f;
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
            self->dead |= self->loop.cells;
            for (int i = 0; i < DW2_GRID_CELLS; i++) if (self->loop.cells & dw2_bit(i)) { self->cell_hp[i] = 0.0f; dw2_ev(log, DW2_EV_BURN, self->idx, i, 0, 0, 0, 0); }
            self->loop.active = 0;
            self->shatter_events++;
        }
    } else {
        self->loop.active = 0;
    }
}

/* ---- damage ---- */
/* A shot of `raw` damage lands on a cell with multiplier `mult`. Returns the raw damage left over once the cell is destroyed (0 if it stands). */
static float dw2_hit_cell(Dw2Ship *t, int cell, float raw, float mult, int kind, int item, int pierce_on, Dw2EventLog *log) {
    float eff = raw * mult, hp = t->cell_hp[cell];
    int flags = (mult < 1.0f ? DW2_HITF_RESISTED : 0) | (mult > 1.0f ? DW2_HITF_WEAK : 0);
    (void)kind;
    if (eff >= hp) {
        float spent = mult > 0.0f ? hp / mult : raw;
        t->cell_hp[cell] = 0.0f;
        t->dead |= dw2_bit(cell);
        dw2_ev(log, DW2_EV_HIT, t->idx, cell, dw2_round(hp), 0, flags | DW2_HITF_DESTROYED | (pierce_on ? DW2_HITF_PIERCED : 0), item);
        return raw - spent;
    }
    t->cell_hp[cell] = hp - eff;
    dw2_ev(log, DW2_EV_HIT, t->idx, cell, dw2_round(eff), dw2_round(hp - eff), flags, item);
    return 0.0f;
}

static void dw2_hit_hull(Dw2Ship *t, float raw, int item, int lane, Dw2EventLog *log) {
    float d = raw * DW2_HULL_FACTOR;
    if (d > t->hull_pct) d = t->hull_pct;
    t->hull_pct -= d;
    if (t->hull_pct < 0.0f) t->hull_pct = 0.0f;
    dw2_ev(log, DW2_EV_HULL, t->idx, dw2_round(d), dw2_round(t->hull_pct), item, lane, 0);
}

/* One shot of `kind` (DW2_SHOT_*) and `raw` damage, fired by weapon `item`, lands in lane `lane` of `enemy`: what it hits depends on the weapon. (Dodging and a Brace shield
 * are the caller's: they come before this.) */
static void dw2_shot_land(Dw2Ship *enemy, int kind, int item, int lane, float raw, Dw2EventLog *log) {
    int8_t eowner[DW2_GRID_CELLS];
    dw2_owner_map(enemy, eowner);
    if (kind == DW2_SHOT_LASER) {
        int hit = -1;
        for (int r = 0; r < DW2_GRID_H; r++) { int c = dw2_cell(r, lane); if (dw2_cell_alive(enemy, c)) { hit = c; break; } }
        if (hit < 0) dw2_hit_hull(enemy, raw, item, lane, log);
        else dw2_hit_cell(enemy, hit, raw, dw2_catalog[enemy->placements[eowner[hit]].item_id].x_laser, kind, item, 0, log);
    } else if (kind == DW2_SHOT_RAIL) {
        float rem = raw; int stopped = 0;
        for (int r = 0; r < DW2_GRID_H && !stopped; r++) {
            int c = dw2_cell(r, lane);
            if (!dw2_cell_alive(enemy, c)) continue;
            float left = dw2_hit_cell(enemy, c, rem, dw2_catalog[enemy->placements[eowner[c]].item_id].x_rail, kind, item, 1, log);
            if (left <= 0.001f) stopped = 1; else rem = left * DW2_RAIL_CARRY;
        }
        if (!stopped) dw2_hit_hull(enemy, rem, item, lane, log);
    } else {
        int hit = -1;
        for (int r = DW2_GRID_H - 1; r >= 0; r--) { int c = dw2_cell(r, lane); if (dw2_cell_alive(enemy, c)) { hit = c; break; } }
        if (hit < 0) dw2_hit_hull(enemy, raw, item, lane, log);
        else {
            dw2_hit_cell(enemy, hit, raw, dw2_catalog[enemy->placements[eowner[hit]].item_id].x_cannon, kind, item, 0, log);
            /* the blast: the parts touching the one that was hit take DW2_CANNON_SPLASH of the shell */
            for (int k = 0; k < 4; k++) {
                int n = dw2_nb(hit, k);
                if (n >= 0 && dw2_cell_alive(enemy, n)) dw2_hit_cell(enemy, n, raw * DW2_CANNON_SPLASH, dw2_catalog[enemy->placements[eowner[n]].item_id].x_cannon, kind, item, 0, log);
            }
        }
    }
}

/* Phase 2 for one ship: its volley lands on the enemy, one lane at a time. */
static void dw2_ship_resolve(Dw2Ship *self, Dw2Ship *enemy, Dw2EventLog *log) {
    for (int p = 0; p < self->placement_n; p++) {
        if (self->placements[p].item_id < 0 || self->shots[p] <= 0) continue;
        int item = self->placements[p].item_id;
        Dw2WeaponStats ws = dw2_weapon_stats(item);
        int wc = dw2_weapon_cell(self, p), lane = wc % DW2_GRID_W;
        for (int sh = 0; sh < self->shots[p]; sh++) {
            float raw = self->shot_dmg[p];
            dw2_ev(log, DW2_EV_FIRE, self->idx, wc, item, lane, raw > ws.damage * self->dmg_mult * 1.001f, 0);
            if (enemy->hull_pct <= 0.0f) continue;
            if (ws.kind != DW2_SHOT_LASER && enemy->evade_pct > 0.0f) {
                enemy->evade_acc += enemy->evade_pct;
                if (enemy->evade_acc >= 100.0f) { enemy->evade_acc -= 100.0f; dw2_ev(log, DW2_EV_EVADE, enemy->idx, lane, item, 0, 0, 0); continue; }
            }
            if (enemy->shield > 0.0f) {
                float ab = raw < enemy->shield ? raw : enemy->shield;
                enemy->shield -= ab; raw -= ab;
                dw2_ev(log, DW2_EV_SHIELD, enemy->idx, dw2_round(ab), dw2_round(enemy->shield), item, lane, 0);
                if (raw <= 0.001f) continue;
            }
            dw2_shot_land(enemy, ws.kind, item, lane, raw, log);
        }
    }
}

/* How many shots of `kind` it takes to destroy everything standing in lane `lane` of the ship, with nothing else about the ship in play (every other cell is taken out,
 * so a shell's splash cannot reach another lane; no shield, no dodging). 0 = the lane is already empty; DW2_LANE_SHOTS_MAX = it would take at least that many. */
int dw2_lane_shots_to_open(const Dw2Ship *s, int lane, int kind) {
    Dw2Ship c = *s;
    c.shield = 0.0f; c.evade_pct = 0.0f; c.evade_acc = 0.0f; c.hull_pct = 100.0f;
    for (int i = 0; i < DW2_GRID_CELLS; i++) if (i % DW2_GRID_W != lane) { c.dead |= dw2_bit(i); c.cell_hp[i] = 0.0f; }
    float raw = kind == DW2_SHOT_LASER ? dw2_weapon_stats(DW2_ITEM_PULSE_LASER).damage : kind == DW2_SHOT_RAIL ? dw2_weapon_stats(DW2_ITEM_RAILGUN).damage : dw2_weapon_stats(DW2_ITEM_SIEGE_CANNON).damage;
    int item = kind == DW2_SHOT_LASER ? DW2_ITEM_PULSE_LASER : kind == DW2_SHOT_RAIL ? DW2_ITEM_RAILGUN : DW2_ITEM_SIEGE_CANNON;
    int shots = 0;
    for (;;) {
        int alive = 0;
        for (int r = 0; r < DW2_GRID_H; r++) if (dw2_cell_alive(&c, dw2_cell(r, lane))) alive++;
        if (!alive) return shots;
        if (shots >= DW2_LANE_SHOTS_MAX) return shots;
        dw2_shot_land(&c, kind, item, lane, raw, NULL);
        shots++;
    }
}

/* Phase 3 for one ship: heat, growth, repair, mending. */
static void dw2_ship_upkeep(Dw2Ship *s, Dw2EventLog *log) {
    int8_t owner[DW2_GRID_CELLS];
    dw2_owner_map(s, owner);
    /* heat: a Quantum Core cooks each uncooled cell that touches it (every cell is decided from the state at the start of the upkeep) */
    int heat[DW2_GRID_CELLS];
    for (int i = 0; i < DW2_GRID_CELLS; i++) heat[i] = dw2_heat_at(s, owner, i);
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        if (heat[i] <= 0 || !dw2_cell_alive(s, i)) continue;
        int src = -1;
        for (int k = 0; k < 4 && src < 0; k++) { int n = dw2_nb(i, k); if (n >= 0 && owner[n] >= 0 && owner[n] != owner[i] && dw2_cell_alive(s, n) && dw2_catalog[s->placements[owner[n]].item_id].heat) src = n; }
        s->cell_hp[i] -= (float)heat[i];
        int dead = 0;
        if (s->cell_hp[i] <= 0.0f) { s->cell_hp[i] = 0.0f; s->dead |= dw2_bit(i); dead = 1; }
        dw2_ev(log, DW2_EV_HEAT, s->idx, i, heat[i], dw2_round(s->cell_hp[i]), dead, src < 0 ? 0 : src);
    }
    /* growth: Xeno-Biomass grows back every living cell it touches */
    float grow[DW2_GRID_CELLS]; int gsrc[DW2_GRID_CELLS];
    for (int i = 0; i < DW2_GRID_CELLS; i++) { grow[i] = 0.0f; gsrc[i] = -1; }
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        if (!dw2_is_role(s, owner, i, DW2_ROLE_BIOMASS)) continue;
        for (int k = 0; k < 4; k++) {
            int n = dw2_nb(i, k);
            if (n < 0 || owner[n] < 0 || !dw2_cell_alive(s, n)) continue;
            grow[n] += owner[n] == owner[i] ? DW2_BIOMASS_HEAL_OWN : DW2_BIOMASS_HEAL_OTHER;
            if (gsrc[n] < 0) gsrc[n] = i;
        }
    }
    for (int i = 0; i < DW2_GRID_CELLS; i++) {
        if (grow[i] <= 0.0f || s->cell_hp[i] >= s->cell_max[i]) continue;
        float before = s->cell_hp[i];
        s->cell_hp[i] += grow[i];
        if (s->cell_hp[i] > s->cell_max[i]) s->cell_hp[i] = s->cell_max[i];
        dw2_ev(log, DW2_EV_REPAIR, s->idx, i, dw2_round(s->cell_hp[i] - before), dw2_round(s->cell_hp[i]), gsrc[i], 1);
    }
    /* repair: a fed Aegis Emitter mends the part in its 3x3 that is closest to breaking; a fed Mender mends the hull */
    if (s->hull_pct > 0.0f) {
        for (int i = 0; i < DW2_GRID_CELLS; i++) {
            if (!(s->pin[i] > 0.0f) || !dw2_cell_alive(s, i)) continue;
            if (dw2_is_role(s, owner, i, DW2_ROLE_MENDER)) {
                float h = DW2_MENDER_HEAL * (s->pin[i] / DW2_GENERATOR_OUTPUT), before = s->hull_pct;
                s->hull_pct += h;
                if (s->hull_pct > 100.0f) s->hull_pct = 100.0f;
                if (s->hull_pct > before) dw2_ev(log, DW2_EV_HEAL, s->idx, i, dw2_round((s->hull_pct - before) * 10.0f), dw2_round(s->hull_pct), 0, 0);
            } else if (dw2_is_role(s, owner, i, DW2_ROLE_AEGIS)) {
                int row = i / DW2_GRID_W, col = i % DW2_GRID_W, best = -1; float bf = 2.0f;
                for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
                    int r = row + dr, c = col + dc;
                    if (r < 0 || r >= DW2_GRID_H || c < 0 || c >= DW2_GRID_W) continue;
                    int n = dw2_cell(r, c);
                    if (!dw2_cell_alive(s, n) || s->cell_hp[n] >= s->cell_max[n]) continue;
                    float fr = s->cell_hp[n] / s->cell_max[n];
                    if (fr < bf) { bf = fr; best = n; }
                }
                if (best >= 0) {
                    float rp = DW2_AEGIS_REPAIR * (s->pin[i] / DW2_GENERATOR_OUTPUT) * dw2_touch_boost(s, owner, owner[i], DW2_FAM_AETHER), before = s->cell_hp[best];
                    s->cell_hp[best] += rp;
                    if (s->cell_hp[best] > s->cell_max[best]) s->cell_hp[best] = s->cell_max[best];
                    dw2_ev(log, DW2_EV_REPAIR, s->idx, best, dw2_round(s->cell_hp[best] - before), dw2_round(s->cell_hp[best]), i, 0);
                }
            }
        }
    }
}

void dw2_ship_tick(Dw2Ship *self, Dw2Ship *enemy, Dw2EventLog *log) {
    dw2_ship_power(self, enemy, log);
    dw2_ship_resolve(self, enemy, log);
    dw2_ship_upkeep(self, log);
}

void dw2_match_tick(Dw2Ship *a, Dw2Ship *b, int tick, Dw2EventLog *log) {
    dw2_ship_power(a, b, log);
    dw2_ship_power(b, a, log);
    if (tick & 1) { dw2_ship_resolve(b, a, log); dw2_ship_resolve(a, b, log); }
    else          { dw2_ship_resolve(a, b, log); dw2_ship_resolve(b, a, log); }
    dw2_ship_upkeep(a, log);
    dw2_ship_upkeep(b, log);
}

Dw2MatchResult dw2_match_result(const Dw2Ship *self, const Dw2Ship *enemy, int elapsed_ticks) {
    int self_dead = self->hull_pct <= 0.0f;
    int enemy_dead = enemy->hull_pct <= 0.0f;
    if (self_dead && enemy_dead) return DW2_RESULT_TIE;
    if (enemy_dead) return DW2_RESULT_SELF_WIN;
    if (self_dead) return DW2_RESULT_ENEMY_WIN;
    if (elapsed_ticks < DW2_MATCH_TIMEOUT_TICKS) return DW2_RESULT_ONGOING;
    /* SPEC_REVIEW.md §2: remaining hull% is the tiebreak; then what is left standing; then what the ship cost. */
    if (self->hull_pct > enemy->hull_pct) return DW2_RESULT_SELF_WIN;
    if (enemy->hull_pct > self->hull_pct) return DW2_RESULT_ENEMY_WIN;
    float sm = dw2_ship_structure_max(self), em = dw2_ship_structure_max(enemy);
    float ss = sm > 0.0f ? dw2_ship_structure(self) / sm : 1.0f, es = em > 0.0f ? dw2_ship_structure(enemy) / em : 1.0f;
    if (ss > es + 0.0001f) return DW2_RESULT_SELF_WIN;
    if (es > ss + 0.0001f) return DW2_RESULT_ENEMY_WIN;
    if (self->cargo_value > enemy->cargo_value) return DW2_RESULT_SELF_WIN;
    if (enemy->cargo_value > self->cargo_value) return DW2_RESULT_ENEMY_WIN;
    return DW2_RESULT_TIE;
}
