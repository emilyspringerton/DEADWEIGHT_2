#include "items.h"

dw2_mask dw2_shape_cells(const Dw2Cell *cells, int n, int rotation, int anchor_row, int anchor_col,
                          Dw2PlacedCell *out) {
    int rdx[DW2_MAX_ITEM_CELLS], rdy[DW2_MAX_ITEM_CELLS];
    int min_dx = 0, min_dy = 0;
    rotation = ((rotation % 4) + 4) % 4;
    for (int i = 0; i < n; i++) {
        int x = cells[i].dx, y = cells[i].dy;
        for (int r = 0; r < rotation; r++) { int nx = -y, ny = x; x = nx; y = ny; }
        rdx[i] = x; rdy[i] = y;
        if (i == 0 || x < min_dx) min_dx = x;
        if (i == 0 || y < min_dy) min_dy = y;
    }
    dw2_mask mask = 0;
    for (int i = 0; i < n; i++) {
        int row = anchor_row + (rdy[i] - min_dy);
        int col = anchor_col + (rdx[i] - min_dx);
        uint8_t port = cells[i].port;
        for (int r = 0; r < rotation; r++) port = dw2_rotate_port_cw(port);
        int ib = row >= 0 && row < DW2_GRID_H && col >= 0 && col < DW2_GRID_W;
        out[i].row = row; out[i].col = col; out[i].port = port; out[i].role = cells[i].role;
        out[i].in_bounds = ib;
        if (ib) mask |= dw2_bit(dw2_cell(row, col));
    }
    return mask;
}

int dw2_split_pieces(int item, int rotation, int anchor_row, int anchor_col, Dw2SplitPiece *out) {
    if (item < 0 || item >= DW2_ITEM_COUNT) return 0;
    const Dw2ItemDef *d = &dw2_catalog[item];
    if (d->split_item < 0 || d->split_n <= 0) return 0;
    Dw2PlacedCell pc[DW2_MAX_ITEM_CELLS];
    dw2_shape_cells(d->base, d->base_n, rotation, anchor_row, anchor_col, pc);
    for (int k = 0; k < d->split_n; k++) {
        int r0 = 99, c0 = 99;
        for (int i = 0; i < d->base_n; i++)
            if (d->split_of[i] == k) { if (pc[i].row < r0) r0 = pc[i].row; if (pc[i].col < c0) c0 = pc[i].col; }
        out[k].item = d->split_item; out[k].row = r0; out[k].col = c0; out[k].rotation = rotation;
    }
    return d->split_n;
}

/* The catalog. Numbers: cost in credits, weight in tons, hp per cell; the three multipliers are what a laser / railgun slug / cannon shell does to a cell of
 * this part as a fraction of its damage. A budget of 1000 credits and 40 tons is a ship of about a dozen parts. */
const Dw2ItemDef dw2_catalog[DW2_ITEM_COUNT] = {
    [DW2_ITEM_GENERATOR] = {
        .name = "Generator", .kind = DW2_KIND_OPERATIONS, .cost = 100, .weight = 4, .hp = 20,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_E, DW2_ROLE_GENERATOR } },
    },
    [DW2_ITEM_CONDUCTOR] = {
        .name = "Conductor", .kind = DW2_KIND_OPERATIONS, .cost = 10, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_CONDUCTOR } },
    },
    [DW2_ITEM_SPLITTER] = {
        .name = "Splitter Node", .kind = DW2_KIND_OPERATIONS, .cost = 25, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        /* one input (W), two outputs (N,S) -- one-to-many is legal (SPEC_REVIEW.md §3) */
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_N | DW2_PORT_S, DW2_ROLE_SPLITTER } },
    },
    [DW2_ITEM_RAILGUN] = {
        /* A slug that goes THROUGH: it punches the first part in its lane and what it did not spend carries on to the next. Fires through its own ship. */
        .name = "Railgun", .kind = DW2_KIND_OFFENSE, .cost = 240, .weight = 9, .hp = 30,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_WEAPON } },
    },
    [DW2_ITEM_BULWARK] = {
        /* A plate: 45 HP a cell and lasers do half to it. The all-round wall. */
        .name = "Bulwark Plate", .kind = DW2_KIND_DEFENSE, .cost = 55, .weight = 5, .hp = 45,
        .x_laser = 0.5f, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, 0, DW2_ROLE_ARMOR } },
    },
    [DW2_ITEM_TITANIUM_BEAM] = {
        /* A 1x4 bar of hard alloy: slugs do half to it, and every weapon touching it hits x1.5 as hard. Splits into two Titanium Shards. */
        .name = "Titanium Beam", .kind = DW2_KIND_OFFENSE, .cost = 90, .weight = 8, .hp = 20,
        .x_laser = 1, .x_rail = 0.5f, .x_cannon = 1, .family = DW2_FAM_TITANIUM, .boost = 1.5f,
        .split_item = DW2_ITEM_TITANIUM_SHARD, .split_n = 2, .split_of = { 0, 0, 1, 1 }, .base_n = 4,
        .base = { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE }, { 2, 0, 0, DW2_ROLE_NONE }, { 3, 0, 0, DW2_ROLE_NONE } },
    },
    [DW2_ITEM_AETHER_ORE] = {
        /* A 2x2 crystal: a prism that scatters beams (lasers do a fifth to it) but shatters under shells; an Aegis Emitter touching it works x1.5.
         * Splits into four Ore Chips. */
        .name = "Aether Ore", .kind = DW2_KIND_DEFENSE, .cost = 100, .weight = 6, .hp = 15,
        .x_laser = 0.2f, .x_rail = 1, .x_cannon = 1.5f, .family = DW2_FAM_AETHER, .boost = 1.5f,
        .split_item = DW2_ITEM_ORE_CHIP, .split_n = 4, .split_of = { 0, 1, 2, 3 }, .base_n = 4,
        .base = { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE }, { 0, 1, 0, DW2_ROLE_NONE }, { 1, 1, 0, DW2_ROLE_NONE } },
    },
    [DW2_ITEM_PULSE_LASER] = {
        /* A fast, light beam: 10 W fires it every second for 7. It needs a clear lane (nothing of yours in front of it) and cannot be dodged. */
        .name = "Pulse Laser", .kind = DW2_KIND_OFFENSE, .cost = 120, .weight = 3, .hp = 35,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_WEAPON } },
    },
    [DW2_ITEM_SIEGE_CANNON] = {
        /* A 1x2 mortar: fed at its west end, the barrel is the second cell. 10 W fires it every 3rd second: a 45 shell that comes down from above on the DEEPEST part in the lane, over any wall. */
        .name = "Siege Cannon", .kind = DW2_KIND_OFFENSE, .cost = 230, .weight = 10, .hp = 30,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 2,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_WEAPON }, { 1, 0, 0, DW2_ROLE_NONE } },
    },
    [DW2_ITEM_AMPLIFIER] = {
        .name = "Amplifier", .kind = DW2_KIND_OPERATIONS, .cost = 90, .weight = 2, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_AMPLIFIER } },
    },
    [DW2_ITEM_MENDER] = {
        /* Fed, it mends 2 hull a second per 10 W. */
        .name = "Mender", .kind = DW2_KIND_OPERATIONS, .cost = 130, .weight = 3, .hp = 15,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_MENDER } },
    },
    [DW2_ITEM_AEGIS] = {
        /* Fed, it repairs 4 HP a second per 10 W to the part in its 3x3 that is closest to breaking (x1.5 touching Aether Ore). */
        .name = "Aegis Emitter", .kind = DW2_KIND_DEFENSE, .cost = 140, .weight = 3, .hp = 15,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_AEGIS } },
    },
    [DW2_ITEM_RESISTOR_1] = {
        .name = "Resistor 1R", .kind = DW2_KIND_OPERATIONS, .cost = 20, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_RESISTOR } },
    },
    [DW2_ITEM_RESISTOR_2] = {
        .name = "Resistor 2R", .kind = DW2_KIND_OPERATIONS, .cost = 20, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_RESISTOR } },
    },
    [DW2_ITEM_RESISTOR_4] = {
        .name = "Resistor 4R", .kind = DW2_KIND_OPERATIONS, .cost = 20, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_RESISTOR } },
    },
    [DW2_ITEM_CAPACITOR] = {
        .name = "Capacitor", .kind = DW2_KIND_OPERATIONS, .cost = 70, .weight = 2, .hp = 15,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_CAPACITOR } },
    },
    [DW2_ITEM_BATTERY] = {
        .name = "Battery", .kind = DW2_KIND_OPERATIONS, .cost = 110, .weight = 4, .hp = 20,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_BATTERY } },
    },
    [DW2_ITEM_TRANSISTOR] = {
        .name = "Transistor", .kind = DW2_KIND_OPERATIONS, .cost = 80, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E | DW2_PORT_N, DW2_ROLE_TRANSISTOR } },
    },
    [DW2_ITEM_DELAYER] = {
        .name = "Delayer", .kind = DW2_KIND_OPERATIONS, .cost = 60, .weight = 1, .hp = 10,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_DELAYER } },
    },
    [DW2_ITEM_QUANTUM_CORE] = {
        /* A 2x2 reactor: each of its four cells is a 10 W source with an output on each of its two outer sides, so 40 W in the space of four Generators... but it
         * runs HOT. Every cell that touches it takes 2 heat damage a second unless it is cooled (a Cryo-Vent, or a cell touching one). */
        .name = "Quantum Core", .kind = DW2_KIND_OPERATIONS, .cost = 320, .weight = 10, .hp = 25,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .heat = 2, .split_item = -1, .base_n = 4,
        .base = { { 0, 0, DW2_PORT_N | DW2_PORT_W, DW2_ROLE_GENERATOR }, { 1, 0, DW2_PORT_N | DW2_PORT_E, DW2_ROLE_GENERATOR },
                  { 0, 1, DW2_PORT_S | DW2_PORT_W, DW2_ROLE_GENERATOR }, { 1, 1, DW2_PORT_S | DW2_PORT_E, DW2_ROLE_GENERATOR } },
    },
    [DW2_ITEM_CRYO_VENT] = {
        .name = "Cryo-Vent", .kind = DW2_KIND_DEFENSE, .cost = 60, .weight = 3, .hp = 20,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 2,
        .base = { { 0, 0, 0, DW2_ROLE_COOLANT }, { 1, 0, 0, DW2_ROLE_COOLANT } },
    },
    [DW2_ITEM_BIOMASS] = {
        /* A bent four-cell colony that grows back what it touches: +2 HP a second to each neighbouring cell of another part (+1 to its own). No power needed. */
        .name = "Xeno-Biomass", .kind = DW2_KIND_DEFENSE, .cost = 90, .weight = 4, .hp = 15,
        .x_laser = 1, .x_rail = 1, .x_cannon = 1, .split_item = -1, .base_n = 4,
        .base = { { 0, 0, 0, DW2_ROLE_BIOMASS }, { 0, 1, 0, DW2_ROLE_BIOMASS }, { 0, 2, 0, DW2_ROLE_BIOMASS }, { 1, 2, 0, DW2_ROLE_BIOMASS } },
    },
    [DW2_ITEM_TITANIUM_SHARD] = {
        /* Half a Beam: two cells that still turn slugs, but boost the weapons they touch only x1.25, and cost and weigh more than half a Beam. */
        .name = "Titanium Shard", .kind = DW2_KIND_OFFENSE, .cost = 55, .weight = 5, .hp = 20,
        .x_laser = 1, .x_rail = 0.5f, .x_cannon = 1, .family = DW2_FAM_TITANIUM, .boost = 1.25f,
        .split_item = -1, .base_n = 2,
        .base = { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE } },
    },
    [DW2_ITEM_ORE_CHIP] = {
        /* A quarter of an Aether Ore: one cell that still scatters beams; an Aegis touching it works x1.25. */
        .name = "Ore Chip", .kind = DW2_KIND_DEFENSE, .cost = 30, .weight = 2, .hp = 15,
        .x_laser = 0.2f, .x_rail = 1, .x_cannon = 1.5f, .family = DW2_FAM_AETHER, .boost = 1.25f,
        .split_item = -1, .base_n = 1,
        .base = { { 0, 0, 0, DW2_ROLE_NONE } },
    },
};
