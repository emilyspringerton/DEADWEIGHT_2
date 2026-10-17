/* dw2_bot -- the D2 bot pool: N persistent bot connections to dw2_server (kind=BOT), each of which sits in the queue
 * until dw2_server pairs it with a human (core pairing rule: a human always gets the oldest waiting bot), packs a
 * strong ship, readies, plays the round-break mini-game, and goes back to the queue.
 *
 * "D4: a real placeholder bot" in NORTHSTAR.md. It is a protocol client like any other (core/protocol.h) -- no
 * server-side special case -- so the browser client, the native client and the bots all meet in one queue.
 *
 *  - Ship: one of apps/bot/loadouts.h, the top of tools/dw2_loadout_search.c's co-evolved search over the real combat
 *    core (layouts, not an LLM). Chosen by the match seed, so a bot is not predictable round to round.
 *  - Round breaks: the server says the exact target press time; the bot presses there, plus a per-bot gaussian-ish
 *    error (--skill ms). A bot mixes its calls: Overcharge when behind (the comeback payoff), Brace when well ahead,
 *    otherwise whichever its dice say -- never the same call forever, which is the whole point of a hidden call.
 *  - It reconnects on its own (the server restarts, a deploy rolls it) and is silent unless --verbose.
 *
 *   dw2_bot --host deadweight2 --port 7800 --count 6 [--skill 90] [--verbose] */
#include "../../core/net.h"
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include "../../core/protocol.h"
#include "../../core/round.h"
#include "loadouts.h"

#define MAX_BOTS 32
enum { B_DOWN = 0, B_HELLO, B_QUEUED, B_PACK, B_COMBAT };

typedef struct {
    dw2_sock fd; int state; uint8_t in[2048]; size_t in_n;
    char name[DW2_NAME_LEN + 1]; int skill; uint32_t rng;
    uint64_t retry_at;                 /* reconnect (B_DOWN) or re-queue (after MATCH_END) */
    uint64_t ready_at, call_at; int ready_pending, call_pending, call_val;
    int hull_you, hull_opp; int seat; uint32_t seed; int pack_ms;
    uint32_t matches;
} Bot;

static Bot bots[MAX_BOTS];
static int n_bots = 6, opt_skill = 90, opt_verbose = 0, opt_port = 7800;
static const char *opt_host = "127.0.0.1";
static volatile sig_atomic_t stop_flag = 0;

static void on_signal(int s) { (void)s; stop_flag = 1; }
static uint32_t brnd(Bot *b) { b->rng ^= b->rng << 13; b->rng ^= b->rng >> 17; b->rng ^= b->rng << 5; return b->rng; }
static int bi(Bot *b, int n) { return (int)(brnd(b) % (uint32_t)n); }
/* roughly bell shaped: four uniforms summed, scaled so the standard deviation is about `skill` ms */
static int jitter(Bot *b, int skill) {
    int s = 0; for (int i = 0; i < 4; i++) s += bi(b, 2 * skill + 1) - skill;
    return s * 87 / 100;
}
static void vlog(Bot *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void vlog(Bot *b, const char *fmt, ...) {
    if (!opt_verbose) return;
    va_list ap; va_start(ap, fmt); fprintf(stderr, "dw2_bot %s: ", b->name); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
}

static void send_msg(Bot *b, const Dw2WireMsg *m) {
    uint8_t buf[DW2_MAX_FRAME_LEN + 2];
    int n = dw2_encode(m, buf, sizeof buf);
    if (n < 0 || b->fd == DW2_BAD_SOCK) return;
    long off = 0;
    while (off < n) {
        long w = dw2_send(b->fd, buf + off, (size_t)(n - off));
        if (w > 0) { off += w; continue; }
        if (w < 0 && dw2_wouldblock()) { dw2_sleep_ms(1); continue; }
        dw2_close(b->fd); b->fd = DW2_BAD_SOCK; b->state = B_DOWN; return;
    }
}

static void drop(Bot *b, uint64_t now, int wait_ms) {
    if (b->fd != DW2_BAD_SOCK) dw2_close(b->fd);
    b->fd = DW2_BAD_SOCK; b->state = B_DOWN; b->in_n = 0;
    b->ready_pending = b->call_pending = 0;
    b->retry_at = now + (uint64_t)wait_ms;
}

static void connect_bot(Bot *b, uint64_t now) {
    dw2_sock s = dw2_connect(opt_host, opt_port);
    if (s == DW2_BAD_SOCK) { b->retry_at = now + 3000; return; }
    dw2_nonblock(s);
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    b->fd = s; b->state = B_HELLO; b->in_n = 0; b->retry_at = 0;
    Dw2WireMsg h; memset(&h, 0, sizeof h);
    h.type = DW2_C_HELLO; h.u.hello.proto = DW2_PROTO_VERSION; h.u.hello.kind = DW2_KIND_BOT;
    snprintf(h.u.hello.name, sizeof h.u.hello.name, "%s", b->name);
    send_msg(b, &h);
}

static void queue_up(Bot *b) {
    Dw2WireMsg q; memset(&q, 0, sizeof q); q.type = DW2_C_QUEUE; send_msg(b, &q);
}

static void pack_ship(Bot *b) {
    const Dw2BotLoadout *lo = &dw2_bot_loadouts[(b->seed >> 3) % DW2_BOT_LOADOUT_COUNT];
    for (int i = 0; i < lo->n; i++) {
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_C_PLACE;
        m.u.place.item_id = (uint8_t)lo->op[i].item; m.u.place.anchor_row = (uint8_t)lo->op[i].row;
        m.u.place.anchor_col = (uint8_t)lo->op[i].col; m.u.place.rotation = (uint8_t)lo->op[i].rot;
        send_msg(b, &m);
    }
    for (int i = 0; i < lo->n; i++) if (lo->op[i].cut) {
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_C_PANIC_CUT; m.u.cut.placement_idx = (uint8_t)i; send_msg(b, &m);
    }
}

static void on_msg(Bot *b, const Dw2WireMsg *m, uint64_t now) {
    switch (m->type) {
    case DW2_S_WELCOME: b->state = B_QUEUED; queue_up(b); break;
    case DW2_S_MATCH_FOUND:
        b->state = B_PACK; b->seat = m->u.match_found.seat; b->seed = m->u.match_found.seed; b->pack_ms = m->u.match_found.pack_ms;
        b->hull_you = b->hull_opp = 100;
        pack_ship(b);
        /* a bot looks at its ship for a moment, like a person; it never runs the pack clock down */
        b->ready_at = now + 1500 + (uint64_t)bi(b, 4000); b->ready_pending = 1;
        vlog(b, "match %u vs %s (%s), loadout %u", m->u.match_found.match_id, m->u.match_found.opp_name,
             m->u.match_found.opp_kind == DW2_KIND_BOT ? "bot" : "human", (b->seed >> 3) % DW2_BOT_LOADOUT_COUNT);
        break;
    case DW2_S_COMBAT_START:
        b->state = B_COMBAT; b->ready_pending = 0;
        b->hull_you = m->u.combat_start.hull_you; b->hull_opp = m->u.combat_start.hull_opp; break;
    case DW2_S_TICK: b->hull_you = m->u.tick.hull_you; b->hull_opp = m->u.tick.hull_opp; break;
    case DW2_S_ROUND_BREAK: {
        int behind = m->u.round_break.behind;
        int lead = b->hull_you - b->hull_opp;
        int over_pct = behind ? 85 : lead >= 30 ? 25 : 60;
        b->call_val = bi(b, 100) < over_pct ? DW2_CALL_OVERCHARGE : DW2_CALL_BRACE;
        int at = (int)m->u.round_break.target_ms + jitter(b, b->skill);
        if (at < 30) at = 30;
        if (at > (int)m->u.round_break.budget_ms - 60) at = (int)m->u.round_break.budget_ms - 60;
        b->call_at = now + (uint64_t)at; b->call_pending = 1;
        vlog(b, "round %d: %s at +%d ms (target %d)", m->u.round_break.round_no, b->call_val == DW2_CALL_OVERCHARGE ? "overcharge" : "brace", at, m->u.round_break.target_ms);
        break;
    }
    case DW2_S_MATCH_END:
        vlog(b, "match over: result %d after %d ticks", m->u.match_end.result, m->u.match_end.ticks);
        b->matches++; b->state = B_QUEUED; b->ready_pending = b->call_pending = 0;
        b->retry_at = now + 1500 + (uint64_t)bi(b, 2500);   /* then queue again */
        break;
    case DW2_S_ERROR: vlog(b, "server error %d", m->u.error.code); drop(b, now, 2000); break;
    default: break;
    }
}

static void read_bot(Bot *b, uint64_t now) {
    long n = dw2_recv(b->fd, b->in + b->in_n, sizeof b->in - b->in_n);
    if (n == 0) { drop(b, now, 2000); return; }
    if (n < 0) { if (!dw2_wouldblock()) drop(b, now, 2000); return; }
    b->in_n += (size_t)n;
    size_t off = 0;
    while (off < b->in_n && b->fd != DW2_BAD_SOCK) {
        Dw2WireMsg m; size_t used = 0;
        int r = dw2_decode(b->in + off, b->in_n - off, &m, &used);
        if (r == 0) break;
        if (r < 0) { drop(b, now, 2000); return; }
        off += used; on_msg(b, &m, now);
    }
    if (b->fd != DW2_BAD_SOCK && off) { memmove(b->in, b->in + off, b->in_n - off); b->in_n -= off; }
}

static void timers(Bot *b, uint64_t now) {
    if (b->state == B_DOWN && now >= b->retry_at) connect_bot(b, now);
    else if (b->state == B_QUEUED && b->retry_at && now >= b->retry_at) { b->retry_at = 0; queue_up(b); }
    if (b->ready_pending && now >= b->ready_at) {
        b->ready_pending = 0;
        Dw2WireMsg r; memset(&r, 0, sizeof r); r.type = DW2_C_READY; send_msg(b, &r);
    }
    if (b->call_pending && now >= b->call_at) {
        b->call_pending = 0;
        Dw2WireMsg c; memset(&c, 0, sizeof c); c.type = DW2_C_ROUND_CALL; c.u.round_call.call = (uint8_t)b->call_val; send_msg(b, &c);
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) opt_host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) opt_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i + 1 < argc) n_bots = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--skill") && i + 1 < argc) opt_skill = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) opt_verbose = 1;
        else { fprintf(stderr, "usage: dw2_bot [--host H] [--port N] [--count N] [--skill MS] [--verbose]\n"); return 2; }
    }
    if (n_bots < 1 || n_bots > MAX_BOTS || opt_skill < 5 || opt_skill > 400) { fprintf(stderr, "dw2_bot: bad --count/--skill\n"); return 2; }
    if (dw2_net_init() != 0) return 1;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    static const char *const names[] = { "Ada", "Brig", "Cobalt", "Dune", "Ember", "Fathom", "Gantry", "Halcyon", "Ivory", "Jetty", "Kestrel", "Lumen",
                                         "Moth", "Nacre", "Orrery", "Pylon", "Quill", "Rivet", "Sable", "Tallow", "Umber", "Vesper", "Wick", "Xenon",
                                         "Yarrow", "Zenith", "Argent", "Basalt", "Cinder", "Drift", "Elm", "Fjord" };
    uint64_t t0 = dw2_now_ms();
    for (int i = 0; i < n_bots; i++) {
        Bot *b = &bots[i]; memset(b, 0, sizeof *b);
        b->fd = DW2_BAD_SOCK; b->state = B_DOWN;
        snprintf(b->name, sizeof b->name, "%s Bot", names[i % 32]);
        b->rng = (0x9E3779B9u * (uint32_t)(i + 1) ^ (uint32_t)t0) | 1u;
        b->skill = opt_skill + (i % 3 - 1) * opt_skill / 3;     /* sharper and sloppier bots in one pool */
        b->retry_at = t0 + (uint64_t)i * 250;                   /* connect staggered */
    }
    fprintf(stderr, "dw2_bot: %d bots -> %s:%d (skill ~%d ms)\n", n_bots, opt_host, opt_port, opt_skill);
    while (!stop_flag) {
        struct pollfd pf[MAX_BOTS]; int map[MAX_BOTS]; unsigned n = 0;
        for (int i = 0; i < n_bots; i++) if (bots[i].fd != DW2_BAD_SOCK) { pf[n].fd = bots[i].fd; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = i; }
        if (n) dw2_poll(pf, n, 15); else dw2_sleep_ms(15);
        uint64_t now = dw2_now_ms();
        for (unsigned k = 0; k < n; k++) if (pf[k].revents & (POLLIN | POLLHUP | POLLERR)) read_bot(&bots[map[k]], now);
        for (int i = 0; i < n_bots; i++) timers(&bots[i], now);
    }
    for (int i = 0; i < n_bots; i++) if (bots[i].fd != DW2_BAD_SOCK) dw2_close(bots[i].fd);
    return 0;
}
