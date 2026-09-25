/* DEADWEIGHT_2 core grid representation -- Phase D1 (local-only core loop).
 * Spec source: DEADWEIGHT/NORTHSTAR.md (V0 cut), DEADWEIGHT/docs/PHASE_D1_CORE_LOOP.md (task
 * list), DEADWEIGHT/docs/SPEC_REVIEW.md (resolved rules, referenced by section below).
 *
 * 6x6 grid, uint64_t triple-bitmask: Occupancy (O) / static Blockade (B) / Ruined Real Estate (R).
 * Cell index = row*6+col, 0..35 (bit 36..63 unused). A placement is legal iff
 * shape_mask & (O | B | R) == 0 (SPEC_REVIEW's own real formula, adopted as-is).
 */
#ifndef DW2_GRID_H_INCLUDED
#define DW2_GRID_H_INCLUDED

#include <stdint.h>

#define DW2_GRID_W 6
#define DW2_GRID_H 6
#define DW2_GRID_CELLS (DW2_GRID_W * DW2_GRID_H)
#define DW2_MAX_ITEM_CELLS 6   /* largest base shape in the V0 catalog is 2x2=4; fragments <=4 too */
#define DW2_MAX_PLACEMENTS 16  /* generous headroom over the 6-9 item V0 catalog */

typedef uint64_t dw2_mask;

static inline int dw2_cell(int row, int col) { return row * DW2_GRID_W + col; }
static inline dw2_mask dw2_bit(int cell) { return (dw2_mask)1 << cell; }

/* Port-bit convention, resolved once here per SPEC_REVIEW.md §7: a 4-bit nibble per occupied
 * cell, bit3=N, bit2=E, bit1=S, bit0=W (matches the array's own left-to-right [N,E,S,W] order).
 * A 90-degree clockwise rotation is the cyclic rotate V' = (V >> 1) | ((V & 1) << 3). */
#define DW2_PORT_N 0x8
#define DW2_PORT_E 0x4
#define DW2_PORT_S 0x2
#define DW2_PORT_W 0x1

static inline uint8_t dw2_rotate_port_cw(uint8_t v) {
    return (uint8_t)(((v >> 1) | ((v & 1) << 3)) & 0xF);
}

typedef struct {
    dw2_mask occupied;    /* O: cells currently covered by a placed item/fragment */
    dw2_mask blockade;    /* B: static, pre-authored obstacles (dummy-grid furniture) */
    dw2_mask ruined;      /* R: vacated-by-a-cut real estate, unusable for the rest of the match */
} Dw2Grid;

static inline int dw2_grid_legal(const Dw2Grid *g, dw2_mask shape) {
    return (shape & (g->occupied | g->blockade | g->ruined)) == 0;
}

static inline void dw2_grid_place(Dw2Grid *g, dw2_mask shape) { g->occupied |= shape; }
static inline void dw2_grid_clear(Dw2Grid *g, dw2_mask shape) { g->occupied &= ~shape; }

#endif
