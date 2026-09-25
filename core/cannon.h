/* "Cannon programming" (EMILY/BACKLOG.md SECTION 549, docs/LO_CANNON_PROGRAMMING.md): D2's
 * weapon-fire decision is a real, compiled LO program (cannon/cannon_decision.llll -> .prn ->
 * cannon/cannon_decision_gen.c, PARENA's own real base4 backend), not a hardcoded C `if`. LO's
 * real ceiling is mod-4/2-bit state (see the design doc's own capability audit) -- so the decision
 * takes exactly one real, naturally-4-valued input packed from two live combat booleans, and
 * returns one of two real states, DW2_CANNON_HOLD or DW2_CANNON_FIRE.
 */
#ifndef DW2_CANNON_H
#define DW2_CANNON_H

#define DW2_CANNON_HOLD 0
#define DW2_CANNON_FIRE 1

/* Packs the two live boolean signals dw2_ship_tick already has on hand into cannon_decision's own
 * single I32 state parameter (bit0 = behind, bit1 = shot_is_overkill) and calls the real, PARENA-
 * compiled decision function. Pure, no side effects -- safe to call every tick for every charged
 * weapon. */
int dw2_cannon_decide(int self_behind, int shot_is_overkill);

#endif
