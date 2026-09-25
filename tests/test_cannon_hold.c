/* test_cannon_hold -- a real, live dw2_ship_tick() integration test for the DW2_CANNON_HOLD branch
 * (core/combat.c, docs/LO_CANNON_PROGRAMMING.md), wired to the real cannon_bank_on_safe_lead.llll
 * decision program (not the shipped, always-fire cannon_decision.llll -- see scripts/build.sh's own
 * separate compile line for this binary). Standalone from tests/test_core_loop.c because only one
 * compiled LO decision program can be linked into a given binary at a time (both define the same
 * real `lo_program` symbol) -- see docs/LO_CANNON_PROGRAMMING.md's own "why two binaries" note.
 */
#include "../core/combat.h"
#include <stdio.h>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

int main(void) {
    Dw2Ship attacker; dw2_ship_init(&attacker);
    dw2_ship_place(&attacker, DW2_ITEM_GENERATOR, 0, 0, 0);
    dw2_ship_place(&attacker, DW2_ITEM_CONDUCTOR, 0, 1, 0);
    dw2_ship_place(&attacker, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_start_combat(&attacker);
    Dw2Ship target; dw2_ship_init(&target); dw2_ship_start_combat(&target);
    /* Target never fires back (no weapon placed) and starts at full hull -- attacker is never
     * `behind` and this shot is never `overkill` (100 > DW2_WEAPON_DAMAGE), so cannon_bank_on_safe_
     * lead's real, compiled decision is HOLD (state 0) every tick charge is ready. */

    dw2_ship_tick(&attacker, &target); CHECK(target.hull_pct == 100.0f); /* tick 1: charge 0->10 */
    dw2_ship_tick(&attacker, &target); CHECK(target.hull_pct == 100.0f); /* tick 2: charge 10->20 */
    dw2_ship_tick(&attacker, &target); /* tick 3: charge 20->30, >=threshold, decide() -> HOLD */
    CHECK(target.hull_pct == 100.0f); /* held, not fired */
    CHECK(attacker.weapon_charge[2] == 30.0f); /* NOT reset by the threshold subtraction; the
                                                 * Generator keeps feeding it regardless of the fire
                                                 * decision (dw2_ship_tick's own energy-routing phase
                                                 * runs before the fire decision, unconditionally) --
                                                 * a real, named consequence of this V0 having no
                                                 * charge cap, not a bug (docs/LO_CANNON_PROGRAMMING.md). */
    dw2_ship_tick(&attacker, &target); /* tick 4: charge 30->40, still HOLD, still no cap */
    CHECK(target.hull_pct == 100.0f);
    CHECK(attacker.weapon_charge[2] == 40.0f);

    /* Now simulate some other source of damage already having brought the target into this shot's
     * own overkill range (<= DW2_WEAPON_DAMAGE) -- the SAME decision program must switch to FIRE. */
    target.hull_pct = 10.0f;
    dw2_ship_tick(&attacker, &target); /* tick 5: charge 40->50, overkill=true -> decide() -> FIRE */
    CHECK(target.hull_pct == 0.0f); /* 10 - 15, clamped */
    CHECK(attacker.weapon_charge[2] == 20.0f); /* the threshold-subtraction DID run this time: 50-30 */

    printf("test_cannon_hold: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
