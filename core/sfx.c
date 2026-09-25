/* dw2_sfx -- engine ported near-verbatim from DEADWEIGHT/apps/gui/sfx.c (the real, actual
 * DEADWEIGHT sound-effects implementation -- see core/sfx.h's own doc comment for why there was
 * never a sample file to copy). The Voice/mixer/ADSR/offline-render machinery below is the same
 * engine, dw2_-prefixed; every cue() case is new, composed for D2's own real events using the
 * same synthesis vocabulary (VC/NOTE/arp/chord/noise_hit) DEADWEIGHT's own file established. */
#include "sfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef DW2_SFX_NO_SDL
#include <SDL.h>
#endif

typedef enum { W_SINE, W_SQUARE, W_SAW, W_TRI, W_NOISE } Wave;

typedef struct {
    int active, prio;
    long delay;
    long age, len;
    Wave wave;
    double ph, f, fmul;
    float a, d, s, r;
    float lp0, lp1, lp;
    float lpk; float lpy[2];
    float pan0, pan1;
    float gain, drive;
    uint32_t rng;
} Voice;

static Voice V[DW2_SFX_MAX_VOICES];
static int muted = 0, offline = 0, open_dev = 0;
static uint32_t seed_ctr = 0x9E3779B9u;
static float cur_master = 1.0f;
#ifndef DW2_SFX_NO_SDL
static SDL_AudioDeviceID dev = 0;
#endif

static float mtof(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void add_voice(Wave w, float f0, float f1, float dur, float a, float d, float s, float r, float lp0, float lp1,
                      float pan0, float pan1, float gain, int prio, float drive, float delay_ms, const Dw2SfxOpts *o) {
    int slot = -1, lowest = -1; int lowp = 99;
    for (int i = 0; i < DW2_SFX_MAX_VOICES; i++) {
        if (!V[i].active) { slot = i; break; }
        if (V[i].prio < lowp || (V[i].prio == lowp && lowest >= 0 && V[i].age > V[lowest].age)) { lowp = V[i].prio; lowest = i; }
    }
    if (slot < 0) { if (lowp >= prio) return; slot = lowest; }
    Voice *v = &V[slot]; memset(v, 0, sizeof *v);
    float pitch = powf(2.0f, o->pitch_st / 12.0f);
    v->active = 1; v->prio = prio; v->wave = w;
    v->delay = (long)((delay_ms + o->delay_ms) * DW2_SFX_RATE / 1000.0f);
    v->len = (long)(dur * DW2_SFX_RATE);
    v->f = f0 * pitch; v->fmul = (f1 > 0 && f0 > 0 && dur > 0) ? (double)powf(f1 / f0, 1.0f / (dur * DW2_SFX_RATE)) : 1.0;
    v->a = a; v->d = d; v->s = s; v->r = r;
    v->lp0 = lp0; v->lp1 = lp1 > 0 ? lp1 : lp0;
    if (o->muffle) { if (v->lp0 == 0 || v->lp0 > 650) v->lp0 = 650; if (v->lp1 == 0 || v->lp1 > 650) v->lp1 = 650; }
    v->pan0 = clampf(pan0 + o->pan, -1, 1); v->pan1 = clampf(pan1 + o->pan, -1, 1);
    v->gain = gain * cur_master * (o->intensity > 0 ? o->intensity : 1.0f) * (o->muffle ? 0.8f : 1.0f); v->drive = drive;
    seed_ctr = seed_ctr * 1664525u + 1013904223u; v->rng = seed_ctr | 1u;
}

#define VC(w, f0, f1, dur, a, d, s, r, lp0, lp1, pn0, pn1, g, pr, drv, dl) add_voice(w, f0, f1, dur, a, d, s, r, lp0, lp1, pn0, pn1, g, pr, drv, dl, &o)
#define NOTE(w, n, dur, g, pr, dl, lp) VC(w, mtof(n), mtof(n), dur, 0.004f, dur * 0.7f, 0.0f, 0.03f, lp, lp, 0, 0, g, pr, 0, dl)

static void chord(Dw2SfxOpts o, Wave w, const float *notes, int n, float dur, float a, float g, int prio, float dl, float glide_st) {
    for (int i = 0; i < n; i++) {
        float f = mtof(notes[i]);
        VC(w, f, f * powf(2.0f, glide_st / 12.0f), dur, a, dur * 0.4f, 0.55f, dur * 0.35f, 4200, 4200, -0.15f + 0.1f * i, -0.15f + 0.1f * i, g, prio, 0, dl);
    }
}

static void noise_hit(Dw2SfxOpts o, float dur, float lp0, float lp1, float g, int prio, float dl, float a) {
    VC(W_NOISE, 0, 0, dur, a, dur * 0.8f, 0.0f, 0.02f, lp0, lp1, 0, 0, g, prio, 0, dl);
}

/* ---- D2's own cues ---- */
static void cue(Dw2SfxCue c, Dw2SfxOpts o) {
    const int CL = DW2_SFX_PRIO_CLASH, WP = DW2_SFX_PRIO_WEAPON, HU = DW2_SFX_PRIO_HULL, AR = DW2_SFX_PRIO_ARMOR, AM = DW2_SFX_PRIO_AMBIENCE;
    switch (c) {
    case DW2_SFX_UI_TICK: NOTE(W_SINE, 90, 0.03f, 0.08f, AM, 0, 0); break;

    case DW2_SFX_PLACE: /* confirm blip: a clean quick rise, "snapped into the grid" */
        NOTE(W_SINE, 72, 0.045f, 0.22f, AM, 0, 6000); NOTE(W_SINE, 79, 0.06f, 0.24f, AM, 45, 6000);
        break;
    case DW2_SFX_PLACE_REJECT: /* short buzzy error */
        VC(W_SQUARE, 220, 140, 0.09f, 0.001f, 0.08f, 0, 0.01f, 2200, 2200, 0, 0, 0.2f, AM, 0, 0);
        VC(W_SQUARE, 180, 110, 0.09f, 0.001f, 0.08f, 0, 0.01f, 2200, 2200, 0, 0, 0.16f, AM, 0, 25);
        break;
    case DW2_SFX_CUT_REJECT:
        cue(DW2_SFX_PLACE_REJECT, o);
        break;

    case DW2_SFX_PANIC_CUT: /* item fragments: a sharp crunch, then shards scattering (falling square notes) */
        noise_hit(o, 0.09f, 5000, 900, 0.5f, HU, 0, 0.001f);
        VC(W_SAW, 260, 90, 0.1f, 0.001f, 0.09f, 0, 0.01f, 2000, 400, 0, 0, 0.28f, HU, 1.5f, 0);
        { static const float shard[4] = {84, 79, 74, 68}; for (int i = 0; i < 4; i++) NOTE(W_SQUARE, shard[i], 0.05f, 0.14f, HU, 60 + i * 45, 5000); }
        break;

    case DW2_SFX_READY: /* confirm chime: clean rising two-note ding */
        NOTE(W_SINE, 79, 0.14f, 0.24f, AM, 0, 0); NOTE(W_SINE, 86, 0.22f, 0.26f, AM, 90, 0);
        break;

    case DW2_SFX_MATCH_FOUND: /* connection established: quick ascending square blip pair */
        VC(W_SQUARE, 700, 700, 0.05f, 0.001f, 0.04f, 0, 0.01f, 5000, 5000, 0, 0, 0.2f, AM, 0, 0);
        VC(W_SQUARE, 1050, 1050, 0.07f, 0.001f, 0.06f, 0, 0.01f, 5000, 5000, 0, 0, 0.22f, AM, 0, 70);
        break;

    case DW2_SFX_COMBAT_START: /* generator spinning up: rising sine sweep under a building pad */
        VC(W_SINE, 55, 220, 0.6f, 0.05f, 0.5f, 0.3f, 0.15f, 0, 0, 0, 0, 0.35f, CL, 0, 0);
        VC(W_TRI, 110, 110, 0.65f, 0.3f, 0.3f, 0.4f, 0.2f, 900, 3000, 0, 0, 0.2f, CL, 0, 60);
        noise_hit(o, 0.5f, 1200, 4000, 0.14f, CL, 100, 0.3f);
        break;

    case DW2_SFX_WEAPON_FIRE: /* Railgun: electromagnetic charge-up (rising glide) then a sharp crack + sub punch */
        VC(W_SINE, 300, 2200, 0.14f, 0.001f, 0.13f, 0, 0.01f, 0, 0, 0, 0, 0.22f, WP, 0, 0);   /* charge whine */
        VC(W_SAW, 2200, 400, 0.09f, 0.001f, 0.08f, 0, 0.01f, 7000, 1200, 0, 0, 0.45f, WP, 3.5f, 140);   /* the crack */
        VC(W_SINE, 130, 40, 0.16f, 0.001f, 0.15f, 0, 0.02f, 0, 0, 0, 0, 0.75f, WP, 1.8f, 150);          /* sub punch */
        noise_hit(o, 0.1f, 9000, 2500, 0.35f, WP, 150, 0.001f);
        break;

    case DW2_SFX_HULL_HIT: /* ported near-verbatim from DEADWEIGHT's own SFX_HULL_HIT */
        noise_hit(o, 0.22f, 1400, 180, 0.6f, HU, 0, 0.001f);
        VC(W_SINE, 95, 40, 0.2f, 0.001f, 0.19f, 0, 0.01f, 0, 0, 0, 0, 0.8f, HU, 3.0f, 0);
        break;

    case DW2_SFX_ARMOR_GAIN: /* ported near-verbatim from DEADWEIGHT's own SFX_ARMOR_GAIN ("shing" + lock) */
        VC(W_SINE, 2400, 2700, 0.22f, 0.001f, 0.2f, 0, 0.02f, 0, 0, 0, 0, 0.22f, AR, 0, 0);
        VC(W_SQUARE, 1200, 1350, 0.1f, 0.001f, 0.09f, 0, 0.01f, 5000, 3000, 0, 0, 0.09f, AR, 0, 0);
        noise_hit(o, 0.03f, 3500, 1200, 0.4f, AR, 95, 0.001f); VC(W_SINE, 620, 480, 0.08f, 0.001f, 0.07f, 0, 0.01f, 0, 0, 0, 0, 0.4f, AR, 0, 95);
        break;

    case DW2_SFX_SHATTER: /* Back-EMF loop burnout: an accelerating growl (mirrors the real 1.35x/tick Phi
                              growth) cut off by a distorted burnout pop + debris -- no DEADWEIGHT analog,
                              designed new for this mechanic. */
        VC(W_SAW, 90, 340, 0.5f, 0.02f, 0.45f, 0.4f, 0.08f, 3000, 3000, 0, 0, 0.24f, CL, 2.5f, 0);   /* rising instability */
        VC(W_SQUARE, 180, 680, 0.5f, 0.02f, 0.45f, 0.4f, 0.08f, 2500, 2500, 0, 0, 0.12f, CL, 1.5f, 20);
        VC(W_SAW, 60, 25, 0.35f, 0.001f, 0.3f, 0, 0.05f, 800, 150, 0, 0, 0.9f, CL, 4.0f, 520);       /* the burnout pop */
        noise_hit(o, 0.4f, 8000, 700, 0.45f, CL, 520, 0.001f);                                        /* debris */
        break;

    case DW2_SFX_MATCH_WIN: { /* triumphant resolving major chord, echoing DEADWEIGHT's own BLOCK_CRIT resolve */
        static const float lo[3] = {60, 64, 67}, hi[4] = {72, 76, 79, 84};
        chord(o, W_TRI, lo, 3, 0.9f, 0.1f, 0.22f, CL, 0, 12.0f);
        chord(o, W_TRI, hi, 4, 1.2f, 0.02f, 0.24f, CL, 620, 0.0f);
        for (int i = 0; i < 5; i++) NOTE(W_SINE, 96 + i * 3, 0.3f, 0.13f, CL, 660 + i * 55, 0);
        break;
    }
    case DW2_SFX_MATCH_LOSE: { /* mirror of the win chord: descending minor, low and dry */
        static const float d[4] = {60, 63, 67, 70};
        for (int i = 0; i < 4; i++) NOTE(W_SAW, d[3 - i], 0.4f, 0.16f, CL, i * 90, 900);
        VC(W_SINE, 90, 38, 0.6f, 0.01f, 0.55f, 0, 0.05f, 0, 0, 0, 0, 0.4f, CL, 0, 0);
        break;
    }
    case DW2_SFX_MATCH_TIE: { /* neutral resolving tone, neither triumphant nor defeated */
        static const float n[2] = {67, 67}; chord(o, W_TRI, n, 2, 0.7f, 0.1f, 0.2f, CL, 0, 0.0f);
        break;
    }
    default: break;
    }
}

static uint32_t xr(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void mix_frames(float *out, int frames) {
    for (int n = 0; n < frames; n++) {
        float L = 0, R = 0;
        for (int i = 0; i < DW2_SFX_MAX_VOICES; i++) {
            Voice *v = &V[i];
            if (!v->active) continue;
            if (v->delay > 0) { v->delay--; continue; }
            if (v->age >= v->len) { v->active = 0; continue; }
            float t = (float)v->age / DW2_SFX_RATE, dur = (float)v->len / DW2_SFX_RATE, e;
            if (t < v->a) e = v->a > 0 ? t / v->a : 1;
            else if (t < v->a + v->d) e = 1.0f - (1.0f - v->s) * ((t - v->a) / (v->d > 0 ? v->d : 1));
            else e = v->s;
            if (v->r > 0 && t > dur - v->r) e *= (dur - t) / v->r;
            float x;
            switch (v->wave) {
            case W_SINE: x = sinf(6.2831853f * (float)v->ph); break;
            case W_SQUARE: x = v->ph < 0.5 ? 1.0f : -1.0f; x *= 0.6f; break;
            case W_SAW: x = 2.0f * (float)v->ph - 1.0f; x *= 0.7f; break;
            case W_TRI: x = 4.0f * fabsf((float)v->ph - 0.5f) - 1.0f; break;
            default: x = ((float)(xr(&v->rng) & 0xFFFF) / 32768.0f) - 1.0f; break;
            }
            v->ph += v->f / DW2_SFX_RATE; if (v->ph >= 1.0) v->ph -= 1.0; v->f *= v->fmul;
            if (v->lp0 > 0) {
                float k_t = dur > 0 ? t / dur : 0, fc = v->lp0 + (v->lp1 - v->lp0) * k_t;
                float k = 1.0f - expf(-6.2831853f * fc / DW2_SFX_RATE);
                v->lpy[0] += k * (x - v->lpy[0]); v->lpy[1] += k * (v->lpy[0] - v->lpy[1]); x = v->lpy[1];
            }
            x *= e * v->gain;
            if (v->drive > 0) x = tanhf(x * (1.0f + v->drive)) / (1.0f + v->drive * 0.3f);
            float pan = v->pan0 + (v->pan1 - v->pan0) * (dur > 0 ? t / dur : 0);
            float ang = (pan + 1.0f) * 0.78539816f;
            L += x * cosf(ang); R += x * sinf(ang);
            v->age++;
        }
        out[2 * n] = tanhf(L * 0.55f); out[2 * n + 1] = tanhf(R * 0.55f);
    }
}

int dw2_sfx_render(int16_t *out, int frames) {
    static float tmp[4096 * 2];
    int done = 0;
    while (done < frames) {
        int n = frames - done > 4096 ? 4096 : frames - done;
        mix_frames(tmp, n);
        for (int i = 0; i < n * 2; i++) out[done * 2 + i] = (int16_t)(clampf(tmp[i], -1, 1) * 32000.0f);
        done += n;
    }
    return frames;
}

void dw2_sfx_play(Dw2SfxCue c, Dw2SfxOpts o) {
    if (muted || (!open_dev && !offline) || c < 0 || c >= DW2_SFX_COUNT) return;
#ifndef DW2_SFX_NO_SDL
    if (dev) SDL_LockAudioDevice(dev);
#endif
    switch (c) {   /* loudness trims, same reasoning as DEADWEIGHT's own: the transient/weapon cues
                       must dominate, the thin square/sine timbres need help to read at equal level */
    case DW2_SFX_WEAPON_FIRE: cur_master = 1.8f; break;
    case DW2_SFX_SHATTER: cur_master = 1.6f; break;
    case DW2_SFX_MATCH_WIN: case DW2_SFX_MATCH_LOSE: cur_master = 1.4f; break;
    case DW2_SFX_PANIC_CUT: cur_master = 1.3f; break;
    default: cur_master = 1.0f; break;
    }
    cue(c, o);
    cur_master = 1.0f;
#ifndef DW2_SFX_NO_SDL
    if (dev) SDL_UnlockAudioDevice(dev);
#endif
}
void dw2_sfx_play_simple(Dw2SfxCue c) { Dw2SfxOpts o = {0, 0, 0, 1.0f, 0}; dw2_sfx_play(c, o); }
void dw2_sfx_stop_all(void) {
#ifndef DW2_SFX_NO_SDL
    if (dev) SDL_LockAudioDevice(dev);
#endif
    memset(V, 0, sizeof V);
#ifndef DW2_SFX_NO_SDL
    if (dev) SDL_UnlockAudioDevice(dev);
#endif
}
int dw2_sfx_active_voices(void) { int n = 0; for (int i = 0; i < DW2_SFX_MAX_VOICES; i++) n += V[i].active; return n; }
void dw2_sfx_set_muted(int m) { muted = m; if (m) dw2_sfx_stop_all(); }
int dw2_sfx_is_muted(void) { return muted; }
int dw2_sfx_is_open(void) { return open_dev; }

void dw2_sfx_offline_begin(void) { memset(V, 0, sizeof V); offline = 1; }
void dw2_sfx_offline_end(void) { memset(V, 0, sizeof V); offline = 0; }

#ifndef DW2_SFX_NO_SDL
static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    int frames = len / (int)(2 * sizeof(int16_t));
    dw2_sfx_render((int16_t *)stream, frames);
}
int dw2_sfx_open(void) {
    if (open_dev) return 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 1;
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = DW2_SFX_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev || have.freq != DW2_SFX_RATE || have.format != AUDIO_S16SYS || have.channels != 2) { if (dev) SDL_CloseAudioDevice(dev); dev = 0; return 2; }
    open_dev = 1; SDL_PauseAudioDevice(dev, 0);
    return 0;
}
void dw2_sfx_close(void) { if (dev) { SDL_CloseAudioDevice(dev); dev = 0; } open_dev = 0; }
#else
int dw2_sfx_open(void) { return 1; }
void dw2_sfx_close(void) {}
#endif

int dw2_sfx_write_wav(const char *path, const int16_t *st, int frames) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, rate = DW2_SFX_RATE, brate = DW2_SFX_RATE * 4, fmt = 16; uint16_t pcm = 1, ch = 2, ba = 4, bps = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(st, 4, (size_t)frames, f); fclose(f); return 0;
}

void dw2_sfx_stats(const int16_t *st, int frames, float thresh, float *peak, float *rms, int *last_loud) {
    double sum = 0; float pk = 0; int last = -1;
    for (int i = 0; i < frames; i++) {
        float l = st[2 * i] / 32768.0f, r = st[2 * i + 1] / 32768.0f, m = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if (m > pk) pk = m;
        if (m > thresh) last = i;
        sum += l * l + r * r;
    }
    if (peak) *peak = pk;
    if (rms) *rms = frames ? (float)sqrt(sum / (2.0 * frames)) : 0;
    if (last_loud) *last_loud = last;
}
