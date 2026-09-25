#include "protocol.h"
#include <string.h>

typedef struct { uint8_t *p; size_t cap, n; int bad; } W;
static void w8(W *w, unsigned v) { if (w->n + 1 > w->cap) { w->bad = 1; return; } w->p[w->n++] = (uint8_t)v; }
static void w16(W *w, unsigned v) { w8(w, v & 255); w8(w, (v >> 8) & 255); }
static void w32(W *w, uint32_t v) { w16(w, v & 0xFFFF); w16(w, v >> 16); }
static void wname(W *w, const char *s) {
    size_t l = 0; while (l < DW2_NAME_LEN && s[l]) l++;
    for (size_t i = 0; i < DW2_NAME_LEN; i++) w8(w, i < l ? (uint8_t)s[i] : 0);
}

typedef struct { const uint8_t *p; size_t len, n; } R;
static unsigned r8(R *r) { return r->p[r->n++]; }
static unsigned r16(R *r) { unsigned a = r8(r); return a | (r8(r) << 8); }
static uint32_t r32(R *r) { uint32_t a = r16(r); return a | ((uint32_t)r16(r) << 16); }
static void rname(R *r, char *out) {
    memcpy(out, r->p + r->n, DW2_NAME_LEN); r->n += DW2_NAME_LEN; out[DW2_NAME_LEN] = 0;
    size_t l = strlen(out); memset(out + l, 0, DW2_NAME_LEN + 1 - l);
}

int dw2_encode(const Dw2WireMsg *m, uint8_t *buf, size_t cap) {
    W w = { buf, cap, 0, 0 };
    w16(&w, 0); /* len patched below */
    w8(&w, m->type);
    switch (m->type) {
    case DW2_C_HELLO:
        if (m->u.hello.token_len > DW2_MAX_TOKEN) return -1;
        w8(&w, m->u.hello.proto); w8(&w, m->u.hello.kind); wname(&w, m->u.hello.name);
        w8(&w, m->u.hello.token_len);
        for (unsigned i = 0; i < m->u.hello.token_len; i++) w8(&w, m->u.hello.token[i]);
        break;
    case DW2_C_AUTH:
        if (m->u.auth.token_len > DW2_MAX_AUTH_TOKEN) return -1;
        w16(&w, m->u.auth.token_len);
        for (unsigned i = 0; i < m->u.auth.token_len; i++) w8(&w, m->u.auth.token[i]);
        break;
    case DW2_C_QUEUE: case DW2_C_READY: case DW2_C_LEAVE: break;
    case DW2_C_PLACE: w8(&w, m->u.place.item_id); w8(&w, m->u.place.anchor_row); w8(&w, m->u.place.anchor_col); w8(&w, m->u.place.rotation); break;
    case DW2_C_PANIC_CUT: w8(&w, m->u.cut.placement_idx); break;
    case DW2_C_PING: case DW2_S_PONG: w32(&w, m->u.ping.nonce); break;
    case DW2_S_WELCOME: w32(&w, m->u.welcome.session_id); w8(&w, m->u.welcome.flags); break;
    case DW2_S_QUEUED: w16(&w, m->u.queued.waiting); break;
    case DW2_S_MATCH_FOUND:
        w32(&w, m->u.match_found.match_id); w32(&w, m->u.match_found.seed); w8(&w, m->u.match_found.seat);
        wname(&w, m->u.match_found.opp_name); w8(&w, m->u.match_found.opp_kind); w16(&w, m->u.match_found.pack_ms);
        break;
    case DW2_S_PLACE_ACK: w8(&w, m->u.place_ack.placement_idx); break;
    case DW2_S_PLACE_REJECT: w8(&w, m->u.place_reject.reason); break;
    case DW2_S_CUT_ACK: w8(&w, m->u.cut_ack.placement_idx); break;
    case DW2_S_CUT_REJECT: w8(&w, m->u.cut_reject.reason); break;
    case DW2_S_COMBAT_START:
        w8(&w, m->u.combat_start.hull_you); w8(&w, m->u.combat_start.hull_opp);
        w16(&w, m->u.combat_start.armor_you); w16(&w, m->u.combat_start.armor_opp);
        w16(&w, m->u.combat_start.cargo_you); w16(&w, m->u.combat_start.cargo_opp);
        break;
    case DW2_S_TICK:
        w8(&w, m->u.tick.elapsed_ticks); w8(&w, m->u.tick.hull_you); w8(&w, m->u.tick.hull_opp);
        w16(&w, m->u.tick.armor_you); w16(&w, m->u.tick.armor_opp);
        w8(&w, m->u.tick.shatter_you); w8(&w, m->u.tick.shatter_opp);
        break;
    case DW2_S_MATCH_END: w32(&w, m->u.match_end.match_id); w8(&w, m->u.match_end.result); w8(&w, m->u.match_end.reason); w8(&w, m->u.match_end.ticks); break;
    case DW2_S_ERROR: w8(&w, m->u.error.code); break;
    default: return -1;
    }
    if (w.bad || w.n - 2 > DW2_MAX_FRAME_LEN) return -1;
    buf[0] = (uint8_t)((w.n - 2) & 255); buf[1] = (uint8_t)((w.n - 2) >> 8);
    return (int)w.n;
}

/* Fixed payload sizes (bytes after the type byte); HELLO is variable (>= 19), AUTH >= 2. */
static int payload_size(uint8_t t) {
    switch (t) {
    case DW2_C_HELLO: return -2; case DW2_C_AUTH: return -3;
    case DW2_C_QUEUE: case DW2_C_READY: case DW2_C_LEAVE: return 0;
    case DW2_C_PLACE: return 4; case DW2_C_PANIC_CUT: return 1;
    case DW2_C_PING: case DW2_S_PONG: return 4;
    case DW2_S_WELCOME: return 5; case DW2_S_QUEUED: return 2;
    case DW2_S_MATCH_FOUND: return 28; case DW2_S_PLACE_ACK: return 1; case DW2_S_PLACE_REJECT: return 1;
    case DW2_S_CUT_ACK: return 1; case DW2_S_CUT_REJECT: return 1;
    case DW2_S_COMBAT_START: return 10; case DW2_S_TICK: return 9; case DW2_S_MATCH_END: return 7;
    case DW2_S_ERROR: return 1; default: return -1;
    }
}

int dw2_decode(const uint8_t *buf, size_t len, Dw2WireMsg *out, size_t *consumed) {
    if (len < 2) return 0;
    size_t flen = (size_t)buf[0] | ((size_t)buf[1] << 8);
    if (flen < 1 || flen > DW2_MAX_FRAME_LEN) return -1;
    if (len < 2 + flen) return 0;
    uint8_t type = buf[2];
    int ps = payload_size(type);
    size_t plen = flen - 1;
    if (ps == -1) return -1;
    if (ps >= 0 && (size_t)ps != plen) return -1;
    if (ps == -2 && plen < 19) return -1;
    if (ps == -3 && plen < 2) return -1;
    R r = { buf + 3, plen, 0 };
    memset(out, 0, sizeof *out);
    out->type = type;
    switch (type) {
    case DW2_C_HELLO:
        out->u.hello.proto = (uint8_t)r8(&r); out->u.hello.kind = (uint8_t)r8(&r);
        rname(&r, out->u.hello.name); out->u.hello.token_len = (uint8_t)r8(&r);
        if (out->u.hello.token_len > DW2_MAX_TOKEN || (size_t)out->u.hello.token_len != plen - 19) return -1;
        memcpy(out->u.hello.token, r.p + r.n, out->u.hello.token_len);
        break;
    case DW2_C_AUTH:
        out->u.auth.token_len = (uint16_t)r16(&r);
        if (out->u.auth.token_len > DW2_MAX_AUTH_TOKEN || (size_t)out->u.auth.token_len != plen - 2) return -1;
        memcpy(out->u.auth.token, r.p + r.n, out->u.auth.token_len);
        break;
    case DW2_C_QUEUE: case DW2_C_READY: case DW2_C_LEAVE: break;
    case DW2_C_PLACE:
        out->u.place.item_id = (uint8_t)r8(&r); out->u.place.anchor_row = (uint8_t)r8(&r);
        out->u.place.anchor_col = (uint8_t)r8(&r); out->u.place.rotation = (uint8_t)r8(&r);
        break;
    case DW2_C_PANIC_CUT: out->u.cut.placement_idx = (uint8_t)r8(&r); break;
    case DW2_C_PING: case DW2_S_PONG: out->u.ping.nonce = r32(&r); break;
    case DW2_S_WELCOME: out->u.welcome.session_id = r32(&r); out->u.welcome.flags = (uint8_t)r8(&r); break;
    case DW2_S_QUEUED: out->u.queued.waiting = (uint16_t)r16(&r); break;
    case DW2_S_MATCH_FOUND:
        out->u.match_found.match_id = r32(&r); out->u.match_found.seed = r32(&r); out->u.match_found.seat = (uint8_t)r8(&r);
        rname(&r, out->u.match_found.opp_name); out->u.match_found.opp_kind = (uint8_t)r8(&r);
        out->u.match_found.pack_ms = (uint16_t)r16(&r);
        break;
    case DW2_S_PLACE_ACK: out->u.place_ack.placement_idx = (uint8_t)r8(&r); break;
    case DW2_S_PLACE_REJECT: out->u.place_reject.reason = (uint8_t)r8(&r); break;
    case DW2_S_CUT_ACK: out->u.cut_ack.placement_idx = (uint8_t)r8(&r); break;
    case DW2_S_CUT_REJECT: out->u.cut_reject.reason = (uint8_t)r8(&r); break;
    case DW2_S_COMBAT_START:
        out->u.combat_start.hull_you = (uint8_t)r8(&r); out->u.combat_start.hull_opp = (uint8_t)r8(&r);
        out->u.combat_start.armor_you = (uint16_t)r16(&r); out->u.combat_start.armor_opp = (uint16_t)r16(&r);
        out->u.combat_start.cargo_you = (uint16_t)r16(&r); out->u.combat_start.cargo_opp = (uint16_t)r16(&r);
        break;
    case DW2_S_TICK:
        out->u.tick.elapsed_ticks = (uint8_t)r8(&r); out->u.tick.hull_you = (uint8_t)r8(&r); out->u.tick.hull_opp = (uint8_t)r8(&r);
        out->u.tick.armor_you = (uint16_t)r16(&r); out->u.tick.armor_opp = (uint16_t)r16(&r);
        out->u.tick.shatter_you = (uint8_t)r8(&r); out->u.tick.shatter_opp = (uint8_t)r8(&r);
        break;
    case DW2_S_MATCH_END: out->u.match_end.match_id = r32(&r); out->u.match_end.result = (uint8_t)r8(&r); out->u.match_end.reason = (uint8_t)r8(&r); out->u.match_end.ticks = (uint8_t)r8(&r); break;
    case DW2_S_ERROR: out->u.error.code = (uint8_t)r8(&r); break;
    default: return -1;
    }
    *consumed = 2 + flen;
    return 1;
}
