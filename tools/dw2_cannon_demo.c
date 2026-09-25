/* dw2_cannon_demo -- headless, scripted proof that a SECOND, real LO cannon-decision program
 * (cannon/cannon_bank_on_safe_lead.llll) compiles through the real LO -> .prn -> PARENA -> C
 * pipeline and produces genuinely different decisions than the shipped cannon_decision.llll,
 * across all 4 real input states. Not wired into any live match -- see docs/LO_CANNON_PROGRAMMING.md
 * for why a second live decision program isn't this V0's job (a real, named, deferred follow-up).
 * Exits nonzero with a clear message on any mismatch (fail clean, matching this repo's own
 * established discipline elsewhere -- e.g. Panic Cut, round-break call handling).
 */
#include <stdio.h>

extern int lo_program(int state);

int main(void) {
    /* state = (shot_is_overkill << 1) | self_behind, per core/cannon.h's own packing. */
    static const struct { int state, want, self_behind, shot_is_overkill; } cases[4] = {
        {0, 0, 0, 0}, /* safe lead, non-lethal shot -> HOLD (bank it) */
        {1, 1, 1, 0}, /* behind, non-lethal shot     -> FIRE (never hesitate when losing) */
        {2, 1, 0, 1}, /* ahead, lethal shot          -> FIRE (take the free kill) */
        {3, 1, 1, 1}, /* behind, lethal shot         -> FIRE (the comeback kill) */
    };
    int failures = 0;
    for (int i = 0; i < 4; i++) {
        int got = lo_program(cases[i].state);
        const char *verdict = got == cases[i].want ? "ok" : "FAIL";
        if (got != cases[i].want) failures++;
        printf("%s state=%d (behind=%d overkill=%d) -> %d (want %d)\n", verdict, cases[i].state,
               cases[i].self_behind, cases[i].shot_is_overkill, got, cases[i].want);
    }
    if (failures) {
        fprintf(stderr, "dw2_cannon_demo: %d/4 cases FAILED\n", failures);
        return 1;
    }
    printf("dw2_cannon_demo: cannon_bank_on_safe_lead correctly banks only on a safe, non-lethal "
           "lead and fires in every other real state\n");
    return 0;
}
