#include "dummy.h"

void dw2_build_dummy_ship(Dw2Ship *s) {
    dw2_ship_init(s);
    /* Generator(0,0) --E--> Conductor(0,1) --E--> Railgun(0,2): a straight, legal, unrotated
     * chain -- no split, no loop, "enough to have a fight to watch/tune numbers against." */
    dw2_ship_place(s, DW2_ITEM_GENERATOR, 0, 0, 0);
    dw2_ship_place(s, DW2_ITEM_CONDUCTOR, 0, 1, 0);
    dw2_ship_place(s, DW2_ITEM_RAILGUN, 0, 2, 0);
    /* A standalone Bulwark Plate for a real, non-zero starting armor pool. */
    dw2_ship_place(s, DW2_ITEM_BULWARK, 3, 3, 0);
    dw2_ship_start_combat(s);
}
