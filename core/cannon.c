#include "cannon.h"

/* Implemented by the real, PARENA-generated cannon/cannon_decision_gen.c (do not edit by hand --
 * regenerate via scripts/generate_cannon.sh from cannon/cannon_decision.llll). The shipped, live
 * policy is deliberately conservative -- FIRE in all 4 states, byte-identical to the hardcoded
 * `if (charge >= threshold) fire` it replaces -- see docs/LO_CANNON_PROGRAMMING.md for why. */
extern int lo_program(int state);

int dw2_cannon_decide(int self_behind, int shot_is_overkill) {
    int state = (shot_is_overkill ? 2 : 0) | (self_behind ? 1 : 0);
    return lo_program(state);
}
