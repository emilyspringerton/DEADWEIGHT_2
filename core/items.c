#include "items.h"

int dw2_concat_fragments(const Dw2ItemDef *def, Dw2Cell *out) {
    int n = dw2_fragment_count(def->split_class);
    int total = 0;
    for (int f = 0; f < n; f++)
        for (int i = 0; i < def->frag_n[f]; i++)
            out[total++] = def->frag[f][i];
    return total;
}

dw2_mask dw2_shape_cells(const Dw2Cell *cells, int n, int rotation, int anchor_row, int anchor_col,
                          Dw2PlacedCell *out) {
    int rdx[DW2_MAX_CUT_CELLS], rdy[DW2_MAX_CUT_CELLS];
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

/* Fragment tables (SPEC_REVIEW.md §1): authored per item, each fragment costs MORE total grid
 * space than the whole item did (the actual fragmentation tax) -- a fragment's own Dead Square
 * cells make up the difference and become armor at combat time (NORTHSTAR.md). Both worked
 * examples from the spec review's own table are reproduced faithfully below (Titanium Beam
 * halves, Aether Ore quarters); Generator/Conductor/Splitter/Railgun/Bulwark are unsplittable
 * functional pieces (splitting a weapon or a wire mid-fight isn't a meaningful choice for V0). */
const Dw2ItemDef dw2_catalog[DW2_ITEM_COUNT] = {
    [0] = {
        .name = "Generator", .kind = DW2_KIND_OPERATIONS, .value = 200,
        .split_class = DW2_SPLIT_NONE, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_E, DW2_ROLE_GENERATOR } },
    },
    [1] = {
        .name = "Conductor", .kind = DW2_KIND_OPERATIONS, .value = 40,
        .split_class = DW2_SPLIT_NONE, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_E, DW2_ROLE_CONDUCTOR } },
    },
    [2] = {
        .name = "Splitter Node", .kind = DW2_KIND_OPERATIONS, .value = 120,
        .split_class = DW2_SPLIT_NONE, .base_n = 1,
        /* one input (W), two outputs (N,S) -- one-to-many is legal (SPEC_REVIEW.md §3) */
        .base = { { 0, 0, DW2_PORT_W | DW2_PORT_N | DW2_PORT_S, DW2_ROLE_SPLITTER } },
    },
    [3] = {
        .name = "Railgun", .kind = DW2_KIND_OFFENSE, .value = 300,
        .split_class = DW2_SPLIT_NONE, .base_n = 1,
        .base = { { 0, 0, DW2_PORT_W, DW2_ROLE_WEAPON } },
    },
    [4] = {
        .name = "Bulwark Plate", .kind = DW2_KIND_DEFENSE, .value = 250,
        .split_class = DW2_SPLIT_NONE, .base_n = 1,
        .base = { { 0, 0, 0, DW2_ROLE_ARMOR } },
    },
    [5] = {
        /* Titanium Beam (1x4 line) | Halves | fragment 1x3 (2 base + 1 dead) -- SPEC_REVIEW.md §1
         * table, verbatim. Base: 4 cells in a row. Each half-fragment needs its OWN row (3 cells
         * doesn't fit inside the original 4-cell line alongside its twin) -- the fragmentation tax
         * made concrete as "you now need floor space you didn't need before," not just a number. */
        .name = "Titanium Beam", .kind = DW2_KIND_OFFENSE, .value = 180,
        .split_class = DW2_SPLIT_HALVABLE, .base_n = 4,
        .base = { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE },
                  { 2, 0, 0, DW2_ROLE_NONE }, { 3, 0, 0, DW2_ROLE_NONE } },
        .frag_n = { 3, 3 },
        .frag = {
            { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE }, { 2, 0, 0, DW2_ROLE_DEAD } },
            { { 0, 1, 0, DW2_ROLE_NONE }, { 1, 1, 0, DW2_ROLE_NONE }, { 2, 1, 0, DW2_ROLE_DEAD } },
        },
    },
    [6] = {
        /* Aether Ore (2x2 square) | Quarters | fragment 1x2 (1 base + 1 dead) -- SPEC_REVIEW.md §1
         * table, verbatim. Four fragments stacked into four rows below the anchor: 2x2=4 cells
         * becomes 4x2=8 cells post-cut, the largest fragmentation tax in the V0 catalog. */
        .name = "Aether Ore", .kind = DW2_KIND_DEFENSE, .value = 220,
        .split_class = DW2_SPLIT_QUARTERABLE, .base_n = 4,
        .base = { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_NONE },
                  { 0, 1, 0, DW2_ROLE_NONE }, { 1, 1, 0, DW2_ROLE_NONE } },
        .frag_n = { 2, 2, 2, 2 },
        .frag = {
            { { 0, 0, 0, DW2_ROLE_NONE }, { 1, 0, 0, DW2_ROLE_DEAD } },
            { { 0, 1, 0, DW2_ROLE_NONE }, { 1, 1, 0, DW2_ROLE_DEAD } },
            { { 0, 2, 0, DW2_ROLE_NONE }, { 1, 2, 0, DW2_ROLE_DEAD } },
            { { 0, 3, 0, DW2_ROLE_NONE }, { 1, 3, 0, DW2_ROLE_DEAD } },
        },
    },
};
