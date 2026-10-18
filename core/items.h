/* DEADWEIGHT_2 item catalog -- ruleset 2.
 *
 * Every part has a COST (credits) and a WEIGHT (tons); a ship may carry at most DW2_BUDGET_CREDITS and DW2_BUDGET_WEIGHT, and there is no limit on the
 * NUMBER of parts but the 36 cells of the grid. Every cell of a part has HP and takes damage from the shots that reach it, so where a part sits decides
 * what it protects, what protects it, what it feeds and what it boosts (see combat.h for the rules of a fight).
 *
 * Origin: the DEADWEIGHT founding transcript (DEADWEIGHT/LeetCode Skills Course Curriculum.pdf): a fragmentation tax on cut items, Quantum Cores that run hot
 * and need Cryo-Vents, Xeno-Biomass that heals what it touches, Titanium Beams that boost the guns beside them, "the physical geometry of your cargo is the
 * ship". Cutting is now a plain in-place split: a Titanium Beam becomes two Titanium Shards, an Aether Ore four Ore Chips, each an ordinary part with its own
 * cost and weight, laid on exactly the cells the whole part held. The tax is that the pieces cost and weigh more than the whole and boost less.
 */
#ifndef DW2_ITEMS_H
#define DW2_ITEMS_H

#include "grid.h"

#define DW2_ITEM_COUNT 24

#define DW2_BUDGET_CREDITS 2618   /* 1000 x phi x phi */
#define DW2_EVADE_REF_WEIGHT 40    /* dodge is still measured from the original 40 t, so the bigger budget does not make every ship slippery */
#define DW2_BUDGET_WEIGHT  105   /* 40 t x phi x phi = 104.7, rounded */

/* Stable indices into dw2_catalog[]. 0-18 are older ids and keep their numbers; the rest are appended. */
enum {
    DW2_ITEM_GENERATOR = 0,
    DW2_ITEM_CONDUCTOR,
    DW2_ITEM_SPLITTER,
    DW2_ITEM_RAILGUN,
    DW2_ITEM_BULWARK,
    DW2_ITEM_TITANIUM_BEAM,
    DW2_ITEM_AETHER_ORE,
    DW2_ITEM_PULSE_LASER,
    DW2_ITEM_SIEGE_CANNON,
    DW2_ITEM_AMPLIFIER,
    DW2_ITEM_MENDER,
    DW2_ITEM_AEGIS,
    DW2_ITEM_RESISTOR_1,
    DW2_ITEM_RESISTOR_2,
    DW2_ITEM_RESISTOR_4,
    DW2_ITEM_CAPACITOR,
    DW2_ITEM_BATTERY,
    DW2_ITEM_TRANSISTOR,
    DW2_ITEM_DELAYER,
    DW2_ITEM_QUANTUM_CORE,
    DW2_ITEM_CRYO_VENT,
    DW2_ITEM_BIOMASS,
    DW2_ITEM_TITANIUM_SHARD,
    DW2_ITEM_ORE_CHIP,
};

typedef enum {
    DW2_ROLE_NONE = 0,     /* a cell with no wiring function: it is a body (HP, and perhaps a material effect) */
    DW2_ROLE_GENERATOR,
    DW2_ROLE_CONDUCTOR,
    DW2_ROLE_SPLITTER,     /* one input port, two output ports: one-to-many is legal (SPEC_REVIEW §3) */
    DW2_ROLE_WEAPON,
    DW2_ROLE_ARMOR,        /* a plate: HP and nothing else, does not conduct */
    DW2_ROLE_DEAD,         /* unused in ruleset 2 (kept so old ids stay put) */
    DW2_ROLE_AMPLIFIER,    /* a wire that lifts the energy passing through it by DW2_AMPLIFIER_GAIN */
    DW2_ROLE_MENDER,       /* fed: mends hull every tick */
    DW2_ROLE_AEGIS,        /* fed: repairs the damaged part nearest to breaking in its 3x3 every tick */
    DW2_ROLE_RESISTOR,     /* a wire that passes only a fraction of the power (the item id says which fraction) */
    DW2_ROLE_CAPACITOR,    /* banks what it is fed and releases it all in one burst when full */
    DW2_ROLE_BATTERY,      /* a reservoir: soaks up what it is fed and puts out a steady supply, even with no generator */
    DW2_ROLE_TRANSISTOR,   /* a switch: the power through its channel flows only while its gate side is energized */
    DW2_ROLE_DELAYER,      /* holds what it is fed for two ticks, then lets it go */
    DW2_ROLE_COOLANT,      /* a Cryo-Vent cell: cooled itself, and it cools every cell that touches it */
    DW2_ROLE_BIOMASS       /* a Xeno-Biomass cell: heals the cells that touch it, every tick, with no power */
} Dw2Role;

typedef enum { DW2_KIND_OFFENSE = 0, DW2_KIND_OPERATIONS, DW2_KIND_DEFENSE } Dw2Kind;

/* material families: parts of a family act on the cells that touch them */
enum { DW2_FAM_NONE = 0, DW2_FAM_TITANIUM, DW2_FAM_AETHER };

typedef struct {
    int8_t dx, dy;     /* offset from the item's anchor cell, pre-rotation */
    uint8_t port;      /* DW2_PORT_* bits, pre-rotation; 0 if this cell doesn't conduct */
    uint8_t role;      /* Dw2Role */
} Dw2Cell;

typedef struct {
    const char *name;
    Dw2Kind kind;
    int cost;         /* credits */
    int weight;       /* tons */
    int hp;           /* hit points of EACH cell */
    /* what a shot does to a cell of this part, as a multiple of its damage: a Bulwark shrugs lasers off, Titanium turns slugs, Aether Ore scatters beams */
    float x_laser, x_rail, x_cannon;
    int family;       /* DW2_FAM_* */
    float boost;      /* family TITANIUM: the damage multiplier of a weapon touching it; family AETHER: the Aegis repair multiplier of an Aegis touching it */
    int heat;         /* heat damage this part's cells give every non-cooled cell they touch, each tick (a Quantum Core) */
    int split_item;   /* the part it can be split into (-1 = none), and how many of them */
    int split_n;
    int8_t split_of[DW2_MAX_ITEM_CELLS];   /* which of the split pieces each base cell becomes */
    int base_n;
    Dw2Cell base[DW2_MAX_ITEM_CELLS];
} Dw2ItemDef;

extern const Dw2ItemDef dw2_catalog[DW2_ITEM_COUNT];

/* Rotates+translates `n` authored cells by `rotation` (0-3, 90 deg clockwise steps) around their
 * own shape origin, then anchors the normalized (non-negative) result at (anchor_row, anchor_col).
 * Writes absolute (row,col) + rotated port nibble + role into `out` (must hold >= n entries) and
 * returns the OR'd occupancy mask. Cells that fall outside the 6x6 grid are dropped from the mask
 * (out-of-bounds is simply illegal -- dw2_grid_legal never sees the bit) but still written to
 * `out` with row/col so an out-of-bounds check can reject the whole placement. */
typedef struct { int row, col; uint8_t port; uint8_t role; int in_bounds; } Dw2PlacedCell;
dw2_mask dw2_shape_cells(const Dw2Cell *cells, int n, int rotation, int anchor_row, int anchor_col,
                          Dw2PlacedCell *out);

/* Splitting a part in place: the pieces (item, rotation, anchor) that lie exactly on the cells the whole part holds when it is laid at (row, col, rotation).
 * Returns the number of pieces (0 if the part cannot be split). piece[k] = {item, row, col, rotation}. */
typedef struct { int item, row, col, rotation; } Dw2SplitPiece;
int dw2_split_pieces(int item, int rotation, int anchor_row, int anchor_col, Dw2SplitPiece *out);

#endif
