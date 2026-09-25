/* DEADWEIGHT_2 item catalog -- Phase D1. V0 cut per DEADWEIGHT/NORTHSTAR.md: 6-9 items, one clean
 * Offense/Operations/Defense triangle member each. Fragment shapes are AUTHORED per item, not
 * computed at runtime (DEADWEIGHT/docs/SPEC_REVIEW.md §1 -- runtime polyomino subdivision is a
 * real, hard combinatorial-geometry problem with no gameplay payoff for a fixed small catalog).
 */
#ifndef DW2_ITEMS_H
#define DW2_ITEMS_H

#include "grid.h"

#define DW2_ITEM_COUNT 7
#define DW2_MAX_FRAGMENTS 4

/* Stable indices into dw2_catalog[] -- see items.c for the authored data these name. */
enum {
    DW2_ITEM_GENERATOR = 0,
    DW2_ITEM_CONDUCTOR,
    DW2_ITEM_SPLITTER,
    DW2_ITEM_RAILGUN,
    DW2_ITEM_BULWARK,
    DW2_ITEM_TITANIUM_BEAM,
    DW2_ITEM_AETHER_ORE,
};

typedef enum {
    DW2_ROLE_NONE = 0,     /* pure cargo cell: no wiring function, contributes value/weight only */
    DW2_ROLE_GENERATOR,
    DW2_ROLE_CONDUCTOR,
    DW2_ROLE_SPLITTER,     /* one input port, two output ports: one-to-many is legal (SPEC_REVIEW §3) */
    DW2_ROLE_WEAPON,
    DW2_ROLE_ARMOR,        /* flat armor contributor, does not conduct */
    DW2_ROLE_DEAD          /* a fragment's Dead Square: inert, becomes armor (NORTHSTAR.md) */
} Dw2Role;

typedef enum { DW2_KIND_OFFENSE = 0, DW2_KIND_OPERATIONS, DW2_KIND_DEFENSE } Dw2Kind;

typedef enum { DW2_SPLIT_NONE = 0, DW2_SPLIT_HALVABLE, DW2_SPLIT_QUARTERABLE } Dw2SplitClass;

typedef struct {
    int8_t dx, dy;     /* offset from the item's anchor cell, pre-rotation */
    uint8_t port;      /* DW2_PORT_* bits, pre-rotation; 0 if this cell doesn't conduct */
    uint8_t role;      /* Dw2Role */
} Dw2Cell;

typedef struct {
    const char *name;
    Dw2Kind kind;
    int value;      /* cargo dollar value -- meta-progression only, never a match win condition
                       (SPEC_REVIEW.md §7: hull% is the sole win condition, cash is not) */
    Dw2SplitClass split_class;
    int base_n;
    Dw2Cell base[DW2_MAX_ITEM_CELLS];
    /* frag_n[i]/frag[i] valid for i in [0, fragment_count()) -- HALVABLE=2, QUARTERABLE=4 */
    int frag_n[DW2_MAX_FRAGMENTS];
    Dw2Cell frag[DW2_MAX_FRAGMENTS][DW2_MAX_ITEM_CELLS];
} Dw2ItemDef;

extern const Dw2ItemDef dw2_catalog[DW2_ITEM_COUNT];

static inline int dw2_fragment_count(Dw2SplitClass c) {
    return c == DW2_SPLIT_HALVABLE ? 2 : c == DW2_SPLIT_QUARTERABLE ? 4 : 0;
}

/* Rotates+translates `n` authored cells by `rotation` (0-3, 90 deg clockwise steps) around their
 * own shape origin, then anchors the normalized (non-negative) result at (anchor_row, anchor_col).
 * Writes absolute (row,col) + rotated port nibble + role into `out` (must hold >= n entries) and
 * returns the OR'd occupancy mask. Cells that fall outside the 6x6 grid are dropped from the mask
 * (out-of-bounds is simply illegal -- dw2_grid_legal never sees the bit) but still written to
 * `out` with row/col so an out-of-bounds check can reject the whole placement. */
typedef struct { int row, col; uint8_t port; uint8_t role; int in_bounds; } Dw2PlacedCell;
dw2_mask dw2_shape_cells(const Dw2Cell *cells, int n, int rotation, int anchor_row, int anchor_col,
                          Dw2PlacedCell *out);

/* Concatenates every fragment tier's authored cells (in the item's own local coordinate frame,
 * NOT yet rotated/anchored) into one flat array, so a cut item's whole fragment set can go through
 * a single dw2_shape_cells() call -- one shared rotation/normalization pass, so fragments that are
 * authored on different local rows (the fragmentation tax "needs its own floor space" shape) keep
 * their relative offsets instead of each being re-normalized to the same row independently.
 * Returns the total cell count; `out` must hold DW2_MAX_ITEM_CELLS * DW2_MAX_FRAGMENTS entries. */
#define DW2_MAX_CUT_CELLS (DW2_MAX_ITEM_CELLS * DW2_MAX_FRAGMENTS)
int dw2_concat_fragments(const Dw2ItemDef *def, Dw2Cell *out);

#endif
