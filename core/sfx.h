/* dw2_sfx -- D2's synthesized sound design, ported from DEADWEIGHT's own apps/gui/sfx.{h,c}
 * (founder real-time, 2026-09-25: "use the actual real deadweight 1 sound effects for the D2
 * game"). DEADWEIGHT's own file names it exactly: "no sample files, no SDL_mixer, no
 * soundfonts" -- its real sound effects ARE this synth engine (sine/square/saw/triangle/noise
 * voices with ADSR, pitch glide, one-pole low-pass, pan, mixed to one stereo stream), not a set
 * of binary assets to copy. There is nothing to `cp` -- there was never a .wav/.ogg/.mp3 file
 * anywhere in DEADWEIGHT (checked directly, `git ls-files | grep -i wav|ogg|mp3` returns
 * nothing); this port carries the actual engine forward and composes D2-specific cues in the
 * same synthesis idiom, the same way core/net.h/http.{h,c}/iduna.{h,c} were ported+adapted from
 * DEADWEIGHT's own infra rather than copied verbatim.
 *
 * The engine (Voice allocation, ADSR envelope, mixing, offline WAV render) is carried over
 * near-verbatim, dw2_-prefixed. The cue set is new and D2-specific -- DEADWEIGHT's own SfxCue
 * enum is entirely card-game-scenario-shaped (SFX_BLITZ/SFX_BLOCK/SFX_BYPASS_LOCK/...), none of
 * which maps onto D2's real events (grid packing, Panic Cut fragmentation, Railgun weapon fire,
 * Back-EMF loop shatter, real-time hull/armor deltas). See core/sfx.c's own cue() for the design
 * reasoning behind each one. */
#ifndef DW2_SFX_H
#define DW2_SFX_H
#include <stdint.h>

#define DW2_SFX_RATE 44100
#define DW2_SFX_MAX_VOICES 20

typedef enum {
    /* packing phase */
    DW2_SFX_UI_TICK,        /* cursor move / item select -- tiny click */
    DW2_SFX_PLACE,          /* legal placement lands -- confirm blip */
    DW2_SFX_PLACE_REJECT,   /* illegal placement -- short error buzz */
    DW2_SFX_PANIC_CUT,      /* a placement fragments -- crunch + glass-shard scatter (legal in packing OR mid-combat) */
    DW2_SFX_CUT_REJECT,     /* cut rejected (unsplittable / no room for fragments) -- error buzz */
    DW2_SFX_READY,          /* READY sent -- confirm chime */
    /* combat */
    DW2_SFX_MATCH_FOUND,    /* opponent found, queue resolved -- short "connection established" blip */
    DW2_SFX_COMBAT_START,   /* ship powering up -- rising swell */
    DW2_SFX_WEAPON_FIRE,    /* Railgun shot: electromagnetic charge-up + crack -- the "cannon" cue */
    DW2_SFX_HULL_HIT,       /* a shot lands (hull_pct dropped since the last tick) */
    DW2_SFX_ARMOR_GAIN,     /* armor increased (Dead Squares exposed by a cut, or initial Bulwark Plate) -- "shing" */
    DW2_SFX_SHATTER,        /* a Back-EMF loop burns out (shatter_events incremented) -- rising unstable growl into a burnout pop */
    DW2_SFX_MATCH_WIN, DW2_SFX_MATCH_LOSE, DW2_SFX_MATCH_TIE,
    DW2_SFX_COUNT
} Dw2SfxCue;

/* Priorities decide which voices survive when the voice cap is hit. */
enum { DW2_SFX_PRIO_AMBIENCE = 0, DW2_SFX_PRIO_ARMOR = 1, DW2_SFX_PRIO_HULL = 2, DW2_SFX_PRIO_WEAPON = 3, DW2_SFX_PRIO_CLASH = 4 };

typedef struct {
    float delay_ms;      /* start offset from now */
    float pan;           /* -1 (left, "you") .. +1 (right, "opponent") */
    float pitch_st;      /* semitones */
    float intensity;     /* 0..1.5: scales volume/weight (e.g. damage size); 1 = normal */
    int muffle;          /* low-pass everything this cue plays (unused today, kept for parity/future use) */
} Dw2SfxOpts;

int  dw2_sfx_open(void);                          /* opens the default SDL audio device; 0 = ok, nonzero = silent (the game still runs) */
void dw2_sfx_close(void);
void dw2_sfx_set_muted(int muted);
int  dw2_sfx_is_muted(void);
int  dw2_sfx_is_open(void);
void dw2_sfx_play(Dw2SfxCue cue, Dw2SfxOpts o);   /* schedules a cue (no-op when there is no device and no offline render in progress) */
void dw2_sfx_play_simple(Dw2SfxCue cue);          /* delay 0, centre, no pitch shift, intensity 1 */
void dw2_sfx_stop_all(void);
int  dw2_sfx_active_voices(void);

/* offline rendering: begin -> dw2_sfx_play(...) -> render N seconds of stereo int16 (interleaved) -> end */
void dw2_sfx_offline_begin(void);
int  dw2_sfx_render(int16_t *out, int frames);    /* mixes `frames` stereo frames, advancing the synth; returns frames */
void dw2_sfx_offline_end(void);
int  dw2_sfx_write_wav(const char *path, const int16_t *stereo, int frames);
void dw2_sfx_stats(const int16_t *stereo, int frames, float thresh, float *peak, float *rms, int *last_loud);
#endif
