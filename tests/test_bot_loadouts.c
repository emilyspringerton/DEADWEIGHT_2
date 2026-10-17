/* Every ship in apps/bot/loadouts.h must still be legal against the live combat core: all ops place, all cuts succeed. A
 * rules or catalog change that breaks one fails the build here, not in a match where a bot shows up with half a ship. */
#include "../core/combat.h"
#include "../apps/bot/loadouts.h"
#include <stdio.h>

int main(void) {
    int bad = 0;
    for (int i = 0; i < DW2_BOT_LOADOUT_COUNT; i++) {
        const Dw2BotLoadout *lo = &dw2_bot_loadouts[i];
        Dw2Ship s; dw2_ship_init(&s);
        for (int k = 0; k < lo->n; k++)
            if (dw2_ship_place(&s, lo->op[k].item, lo->op[k].row, lo->op[k].col, lo->op[k].rot) != k) { printf("loadout %d: op %d does not place\n", i, k); bad++; }
        for (int k = 0; k < lo->n; k++)
            if (lo->op[k].cut && !dw2_ship_panic_cut(&s, k)) { printf("loadout %d: cut %d fails\n", i, k); bad++; }
        dw2_ship_start_combat(&s);
        if (s.placement_n != lo->n) { printf("loadout %d: %d of %d ops placed\n", i, s.placement_n, lo->n); bad++; }
    }
    if (bad) { printf("FAIL: %d problems\n", bad); return 1; }
    printf("bot loadouts: %d ships, all legal against the combat core\n", DW2_BOT_LOADOUT_COUNT);
    return 0;
}
