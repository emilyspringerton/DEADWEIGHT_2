#include "round.h"

/* splitmix32-shaped hash -- good-enough avalanche for a deterministic, non-cryptographic per-round
 * target, not a security boundary (the target is told to the player outright, never secret). */
static uint32_t dw2_hash_u32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint16_t dw2_round_target_ms(uint32_t match_seed, int round_no) {
    uint32_t h = dw2_hash_u32(match_seed ^ ((uint32_t)round_no * 0x9E3779B9u));
    return (uint16_t)(DW2_ROUND_TARGET_MIN_MS + (h % DW2_ROUND_TARGET_MS_SPAN));
}

int dw2_standing(float my_hull, float my_armor, float their_hull, float their_armor) {
    float mine = my_hull + my_armor, theirs = their_hull + their_armor;
    return mine > theirs ? DW2_STAND_AHEAD : mine < theirs ? DW2_STAND_BEHIND : DW2_STAND_LEVEL;
}

Dw2RoundEffect dw2_round_grade(uint32_t match_seed, int round_no, int call, int no_call,
                                uint32_t elapsed_ms, int standing, int *out_grade) {
    Dw2RoundEffect e = { 1.0f, 0.0f, 0.0f };
    if (no_call) { *out_grade = DW2_GRADE_NONE; return e; }

    uint16_t target = dw2_round_target_ms(match_seed, round_no);
    uint32_t diff = elapsed_ms > target ? elapsed_ms - target : target - elapsed_ms;
    int grade = diff <= DW2_ROUND_PERFECT_MS ? DW2_GRADE_PERFECT
              : diff <= DW2_ROUND_GOOD_MS ? DW2_GRADE_GOOD
              : DW2_GRADE_MISS;
    *out_grade = grade;

    /* the catch-up lever: how much a win is worth and how much a miss costs depends on who is ahead */
    float k_win  = standing == DW2_STAND_AHEAD ? DW2_STAND_AHEAD_WIN  : standing == DW2_STAND_BEHIND ? DW2_STAND_BEHIND_WIN  : 1.0f;
    float k_miss = standing == DW2_STAND_AHEAD ? DW2_STAND_AHEAD_MISS : standing == DW2_STAND_BEHIND ? DW2_STAND_BEHIND_MISS : 1.0f;

    if (call == DW2_CALL_OVERCHARGE) {
        if (grade == DW2_GRADE_PERFECT) e.dmg_mult = 1.0f + 0.5f * k_win;
        else if (grade == DW2_GRADE_GOOD) e.dmg_mult = 1.0f + 0.25f * k_win;
        else e.self_damage = DW2_ROUND_OVERCHARGE_BACKFIRE_HULL * k_miss; /* MISS: the risky call backfires */
    } else if (call == DW2_CALL_BRACE) {
        if (grade == DW2_GRADE_PERFECT) e.armor_bonus = 25.0f * k_win;
        else if (grade == DW2_GRADE_GOOD) e.armor_bonus = 12.0f * k_win;
        /* MISS: no bonus, no penalty either -- Brace is the safe, low-ceiling call. */
    }
    return e;
}

void dw2_ship_apply_round_effect(Dw2Ship *s, Dw2RoundEffect e) {
    s->dmg_mult = e.dmg_mult;
    if (e.armor_bonus > 0.0f) { s->armor += e.armor_bonus; s->armor_max += e.armor_bonus; }
    if (e.self_damage > 0.0f) {
        s->hull_pct -= e.self_damage;
        if (s->hull_pct < 0.0f) s->hull_pct = 0.0f;
    }
}
