/* dw2_server -- DEADWEIGHT_2 authoritative match server (D2, EMILY/BACKLOG.md SECTION 546).
 * Single poll() loop hosts the queue and many concurrent packing+combat matches over TCP
 * (core/protocol.h). No blocking calls in the loop -- IDUNA verify/report run on a worker thread,
 * same pattern as DEADWEIGHT/apps/server/main.c.
 *
 * Real, deliberate architecture departure from dw_server (see DEADWEIGHT_2/NORTHSTAR.md's own D2
 * note, corrected during implementation): no separate matchmaker binary. The REDGARDEN/ECOWAR
 * matchmaker lineage (apps/matchmaker) speaks a UDP wire protocol + process-per-match model that
 * doesn't fit DW2's TCP, single-process, real-time-tick design -- so, same as DEADWEIGHT's own
 * dw_server, this binary does its own in-process FIFO queueing.
 *
 * Match lifecycle: QUEUE -> paired -> MATCH_FOUND (packing phase, PLACE/PANIC_CUT/READY, timed by
 * --pack-ms) -> COMBAT_START (both ships locked via dw2_ship_start_combat) -> one dw2_ship_tick()
 * pair per --tick-ms (PANIC_CUT still legal live, per core/combat.h's own doc comment) -> MATCH_END
 * when dw2_match_result() stops returning DW2_RESULT_ONGOING. */
#include "../../core/net.h"
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include "../../core/combat.h"
#include "../../core/iduna.h"
#include "../../core/protocol.h"
#include "version.h"
#ifndef _WIN32
#include <pthread.h>
#define DW2_HAVE_WORKER 1
#endif

#define MAX_CONNS 512
#define MAX_MATCHES 256
#define INBUF 1024
#define OUTBUF 4096
#define PACK_MS_DEFAULT 60000
#define TICK_MS_DEFAULT 1000
#define HELLO_TIMEOUT_MS 10000

enum { S_FREE = 0, S_CONNECTED, S_NEEDAUTH, S_VERIFYING, S_READY, S_QUEUED, S_PACKING, S_COMBAT };

typedef struct {
    dw2_sock fd; int state; uint32_t session_id;
    uint8_t in[INBUF]; size_t in_n;
    uint8_t out[OUTBUF]; size_t out_n;
    char name[DW2_NAME_LEN + 1]; uint8_t kind; char player_id[48];
    int match, seat; uint64_t queued_seq, connect_ms; int close_after_flush;
} Conn;

typedef struct {
    int active; uint32_t id, seed; int conn[2];
    Dw2Ship ship[2]; int ready[2]; uint64_t pack_deadline;
    int combat_started; int elapsed_ticks; uint64_t next_tick_deadline;
} Match;

static Conn conns[MAX_CONNS];
static Match matches[MAX_MATCHES];
static volatile sig_atomic_t stop_flag = 0;
static int opt_noauth = 1, opt_verbose = 0, opt_fast_forward = 0;
static int opt_iduna = 0, opt_fail_open = 0, opt_noauth_explicit = 0;
static Dw2Iduna iduna;
static int opt_pack_ms = PACK_MS_DEFAULT, opt_tick_ms = TICK_MS_DEFAULT;
static uint32_t next_session = 1, next_match = 1, seed_state = 0x9E3779B9u;
static uint64_t queue_seq = 0;
static long st_matches = 0, st_forfeits = 0;

/* ---- IDUNA worker thread: verify + match-result HTTP never runs on the poll() loop ---- */
enum { J_VERIFY = 1, J_REPORT };
typedef struct { int type; int conn; uint32_t gen; char token[DW2_MAX_AUTH_TOKEN + 1]; Dw2MatchReport rep; } Job;
typedef struct { int conn; uint32_t gen; int rc; Dw2Identity id; } VResult;
#ifdef DW2_HAVE_WORKER
#define JQ 256
static Job jq[JQ]; static int jq_n = 0, jq_head = 0;
static VResult rq[JQ]; static int rq_n = 0;
static pthread_mutex_t jm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t jc = PTHREAD_COND_INITIALIZER;
static pthread_t worker_tid; static int worker_started = 0, worker_stop = 0, wake_fd[2] = { -1, -1 };

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&jm);
        while (jq_n == 0 && !worker_stop) pthread_cond_wait(&jc, &jm);
        if (jq_n == 0 && worker_stop) { pthread_mutex_unlock(&jm); return NULL; }
        Job *j = malloc(sizeof *j);
        if (!j) { pthread_mutex_unlock(&jm); return NULL; }
        *j = jq[jq_head]; jq_head = (jq_head + 1) % JQ; jq_n--;
        pthread_mutex_unlock(&jm);
        if (j->type == J_VERIFY) {
            VResult r; memset(&r, 0, sizeof r); r.conn = j->conn; r.gen = j->gen;
            r.rc = dwi2_verify(&iduna, j->token, &r.id);
            pthread_mutex_lock(&jm);
            if (rq_n < JQ) rq[rq_n++] = r;
            pthread_mutex_unlock(&jm);
            if (write(wake_fd[1], "x", 1) < 0) { /* wake pipe full: the loop is already awake */ }
        } else {
            if (dwi2_report(&iduna, &j->rep) != 0)
                fprintf(stderr, "dw2_server: IDUNA match-result report failed for match %u (see log; continuing)\n", j->rep.match_id);
        }
        free(j);
    }
}
static int enqueue_job(const Job *j) {
    int ok = 0;
    pthread_mutex_lock(&jm);
    if (jq_n < JQ) { jq[(jq_head + jq_n) % JQ] = *j; jq_n++; ok = 1; pthread_cond_signal(&jc); }
    pthread_mutex_unlock(&jm);
    return ok;
}
static int start_worker(void) {
    if (pipe(wake_fd) != 0) return -1;
    dw2_nonblock(wake_fd[0]); dw2_nonblock(wake_fd[1]);
    if (pthread_create(&worker_tid, NULL, worker_main, NULL) != 0) return -1;
    worker_started = 1; return 0;
}
static void stop_worker(void) {
    if (!worker_started) return;
    pthread_mutex_lock(&jm); worker_stop = 1; pthread_cond_signal(&jc); pthread_mutex_unlock(&jm);
    pthread_join(worker_tid, NULL); worker_started = 0;
}
#else
static int enqueue_job(const Job *j) { (void)j; return 0; }
static int start_worker(void) { return -1; }
static void stop_worker(void) {}
#endif

static void on_signal(int s) { (void)s; stop_flag = 1; }
static void vlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void vlog(const char *fmt, ...) {
    if (!opt_verbose) return;
    va_list ap; va_start(ap, fmt); fprintf(stderr, "dw2_server: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap);
}

static uint32_t next_seed(void) {
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 17; seed_state ^= seed_state << 5;
    return seed_state ^ (uint32_t)dw2_now_ms();
}

static void conn_close(int ci);

static void flush_conn(int ci) {
    Conn *c = &conns[ci];
    while (c->out_n > 0) {
        long n = dw2_send(c->fd, c->out, c->out_n);
        if (n > 0) { memmove(c->out, c->out + n, c->out_n - (size_t)n); c->out_n -= (size_t)n; }
        else if (n < 0 && dw2_wouldblock()) break;
        else { c->close_after_flush = 1; c->out_n = 0; return; }
    }
}

static void send_msg(int ci, const Dw2WireMsg *m) {
    Conn *c = &conns[ci];
    uint8_t b[DW2_MAX_FRAME_LEN + 2];
    int n = dw2_encode(m, b, sizeof b);
    if (n < 0 || c->state == S_FREE) return;
    if (c->out_n + (size_t)n > OUTBUF) { c->close_after_flush = 1; c->out_n = 0; return; } /* slow reader: drop */
    memcpy(c->out + c->out_n, b, (size_t)n); c->out_n += (size_t)n;
    flush_conn(ci);
}

static void send_error(int ci, int code) {
    Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_S_ERROR; m.u.error.code = (uint8_t)code; send_msg(ci, &m);
    conns[ci].close_after_flush = 1;
}

static uint8_t hull_u8(float v) { if (v < 0) v = 0; if (v > 100) v = 100; return (uint8_t)v; }
static uint16_t clamp_u16(float v) { if (v < 0) v = 0; if (v > 65535) v = 65535; return (uint16_t)v; }

static void end_match(int mi, int reason) {
    Match *mt = &matches[mi];
    Dw2MatchResult r0 = dw2_match_result(&mt->ship[0], &mt->ship[1], mt->elapsed_ticks);
    int result[2];
    if (r0 == DW2_RESULT_TIE) { result[0] = DW2_RES_TIE; result[1] = DW2_RES_TIE; }
    else if (r0 == DW2_RESULT_SELF_WIN) { result[0] = DW2_RES_WIN; result[1] = DW2_RES_LOSS; }
    else { result[0] = DW2_RES_LOSS; result[1] = DW2_RES_WIN; }
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s];
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_S_MATCH_END;
        m.u.match_end.match_id = mt->id; m.u.match_end.result = (uint8_t)result[s];
        m.u.match_end.reason = (uint8_t)reason; m.u.match_end.ticks = (uint8_t)mt->elapsed_ticks;
        send_msg(ci, &m);
        if (conns[ci].state == S_PACKING || conns[ci].state == S_COMBAT) { conns[ci].state = S_READY; conns[ci].match = -1; }
    }
    st_matches++;
    if (reason == DW2_END_FORFEIT) st_forfeits++;
    if (opt_iduna && reason != DW2_END_SERVER && conns[mt->conn[0]].player_id[0] && conns[mt->conn[1]].player_id[0]) {
        Job j; memset(&j, 0, sizeof j); j.type = J_REPORT;
        j.rep.match_id = mt->id; j.rep.seed = mt->seed; j.rep.ticks = mt->elapsed_ticks; j.rep.reason = reason;
        j.rep.winner = result[0] == DW2_RES_WIN ? 0 : result[1] == DW2_RES_WIN ? 1 : 2;
        snprintf(j.rep.seat_pid[0], sizeof j.rep.seat_pid[0], "%s", conns[mt->conn[0]].player_id);
        snprintf(j.rep.seat_pid[1], sizeof j.rep.seat_pid[1], "%s", conns[mt->conn[1]].player_id);
        if (!enqueue_job(&j)) fprintf(stderr, "dw2_server: IDUNA job queue full, match %u result not reported\n", mt->id);
    }
    vlog("match %u ended: result=[%d,%d] reason=%d ticks=%d", mt->id, result[0], result[1], reason, mt->elapsed_ticks);
    mt->active = 0;
}

static void send_tick(Match *mt) {
    for (int s = 0; s < 2; s++) {
        const Dw2Ship *you = &mt->ship[s], *opp = &mt->ship[1 - s];
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_S_TICK;
        m.u.tick.elapsed_ticks = (uint8_t)mt->elapsed_ticks;
        m.u.tick.hull_you = hull_u8(you->hull_pct); m.u.tick.hull_opp = hull_u8(opp->hull_pct);
        m.u.tick.armor_you = clamp_u16(you->armor); m.u.tick.armor_opp = clamp_u16(opp->armor);
        m.u.tick.shatter_you = (uint8_t)you->shatter_events; m.u.tick.shatter_opp = (uint8_t)opp->shatter_events;
        send_msg(mt->conn[s], &m);
    }
}

static void run_tick(int mi) {
    Match *mt = &matches[mi];
    dw2_ship_tick(&mt->ship[0], &mt->ship[1]);
    dw2_ship_tick(&mt->ship[1], &mt->ship[0]);
    mt->elapsed_ticks++;
    send_tick(mt);
    Dw2MatchResult r = dw2_match_result(&mt->ship[0], &mt->ship[1], mt->elapsed_ticks);
    if (r != DW2_RESULT_ONGOING) {
        int reason = (mt->ship[0].hull_pct <= 0.0f || mt->ship[1].hull_pct <= 0.0f) ? DW2_END_HULL : DW2_END_TIMEOUT;
        end_match(mi, reason);
        return;
    }
    mt->next_tick_deadline = opt_fast_forward ? dw2_now_ms() : dw2_now_ms() + (uint64_t)opt_tick_ms;
}

static void start_combat_if_ready(int mi) {
    Match *mt = &matches[mi];
    if (mt->combat_started) return;
    uint64_t now = dw2_now_ms();
    if (!(mt->ready[0] && mt->ready[1]) && now < mt->pack_deadline) return;
    for (int s = 0; s < 2; s++) dw2_ship_start_combat(&mt->ship[s]);
    mt->combat_started = 1;
    mt->elapsed_ticks = 0;
    mt->next_tick_deadline = opt_fast_forward ? now : now + (uint64_t)opt_tick_ms;
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s]; conns[ci].state = S_COMBAT;
        const Dw2Ship *you = &mt->ship[s], *opp = &mt->ship[1 - s];
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_S_COMBAT_START;
        m.u.combat_start.hull_you = hull_u8(you->hull_pct); m.u.combat_start.hull_opp = hull_u8(opp->hull_pct);
        m.u.combat_start.armor_you = clamp_u16(you->armor); m.u.combat_start.armor_opp = clamp_u16(opp->armor);
        m.u.combat_start.cargo_you = clamp_u16((float)you->cargo_value); m.u.combat_start.cargo_opp = clamp_u16((float)opp->cargo_value);
        send_msg(ci, &m);
    }
    vlog("match %u: combat start (%s vs %s)", mt->id, conns[mt->conn[0]].name, conns[mt->conn[1]].name);
}

static int start_match(int a, int b) {
    int mi = -1;
    for (int i = 0; i < MAX_MATCHES; i++) if (!matches[i].active) { mi = i; break; }
    if (mi < 0) return 0;
    Match *mt = &matches[mi]; memset(mt, 0, sizeof *mt);
    mt->active = 1; mt->id = next_match++; mt->seed = next_seed(); mt->conn[0] = a; mt->conn[1] = b;
    dw2_ship_init(&mt->ship[0]); dw2_ship_init(&mt->ship[1]);
    mt->pack_deadline = dw2_now_ms() + (uint64_t)opt_pack_ms;
    for (int s = 0; s < 2; s++) {
        int ci = mt->conn[s], oi = mt->conn[1 - s];
        conns[ci].state = S_PACKING; conns[ci].match = mi; conns[ci].seat = s;
        Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_S_MATCH_FOUND;
        m.u.match_found.match_id = mt->id; m.u.match_found.seed = mt->seed; m.u.match_found.seat = (uint8_t)s;
        memcpy(m.u.match_found.opp_name, conns[oi].name, DW2_NAME_LEN + 1); m.u.match_found.opp_kind = conns[oi].kind;
        m.u.match_found.pack_ms = (uint16_t)opt_pack_ms;
        send_msg(ci, &m);
    }
    vlog("match %u: %s(%d) vs %s(%d) seed=%u", mt->id, conns[a].name, conns[a].kind, conns[b].name, conns[b].kind, mt->seed);
    return 1;
}

static int oldest_queued(int kind, int skip, int *count) {
    int best = -1, n = 0;
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &conns[i];
        if (c->state != S_QUEUED || c->kind != kind) continue;
        n++;
        if (i != skip && (best < 0 || c->queued_seq < conns[best].queued_seq)) best = i;
    }
    if (count) *count = n;
    return best;
}

/* Pairing rule matching dw_server's own: humans first (human-human, then human-oldest-bot); bots
 * only pair with each other while at least one other bot remains waiting, so a late-joining human
 * always finds a bot. */
static void try_pair(void) {
    for (;;) {
        int nh, nb;
        int h1 = oldest_queued(DW2_KIND_HUMAN, -1, &nh);
        int b1 = oldest_queued(DW2_KIND_BOT, -1, &nb);
        if (nh >= 2) { int h2 = oldest_queued(DW2_KIND_HUMAN, h1, NULL); if (!start_match(h1, h2)) break; continue; }
        if (nh == 1 && nb >= 1) { if (!start_match(h1, b1)) break; continue; }
        if (nh == 0 && nb >= 3) { int b2 = oldest_queued(DW2_KIND_BOT, b1, NULL); if (!start_match(b1, b2)) break; continue; }
        break;
    }
}

static int queued_count(void) { int n = 0; for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state == S_QUEUED) n++; return n; }

static void leave_match_or_queue(int ci) {
    Conn *c = &conns[ci];
    if ((c->state == S_PACKING || c->state == S_COMBAT) && c->match >= 0 && matches[c->match].active)
        end_match(c->match, DW2_END_FORFEIT);
    if (c->state == S_QUEUED) c->state = S_READY;
}

static void conn_close(int ci) {
    Conn *c = &conns[ci];
    if (c->state == S_FREE) return;
    leave_match_or_queue(ci);
    dw2_close(c->fd);
    memset(c, 0, sizeof *c); c->fd = DW2_BAD_SOCK; c->state = S_FREE; c->match = -1;
}

static void become_ready(int ci) {
    Conn *c = &conns[ci];
    Dw2WireMsg r; memset(&r, 0, sizeof r);
    c->state = S_READY;
    r.type = DW2_S_WELCOME; r.u.welcome.session_id = c->session_id;
    r.u.welcome.flags = (uint8_t)(opt_noauth ? 0 : DW2_FLAG_AUTH_REQUIRED);
    send_msg(ci, &r);
}

static int token_ok(const uint8_t *t, unsigned n) {
    if (n == 0) return 0;
    for (unsigned i = 0; i < n; i++) if (!((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z') || (t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == '-' || t[i] == '_')) return 0;
    return 1;
}

static void start_verify(int ci, const uint8_t *tok, unsigned n) {
    Conn *c = &conns[ci];
    Job j; memset(&j, 0, sizeof j); j.type = J_VERIFY; j.conn = ci; j.gen = c->session_id;
    if (!token_ok(tok, n)) { send_error(ci, DW2_ERR_AUTH); return; }
    memcpy(j.token, tok, n);
    c->state = S_VERIFYING;
    if (!enqueue_job(&j)) { vlog("IDUNA queue unavailable, refusing conn %d", ci); send_error(ci, DW2_ERR_AUTH); }
}

static void verify_done(const VResult *r) {
    if (r->conn < 0 || r->conn >= MAX_CONNS) return;
    Conn *c = &conns[r->conn];
    if (c->state != S_VERIFYING || c->session_id != r->gen) return;
    if (r->rc == 0) {
        if ((int)c->kind != r->id.kind) { vlog("conn %d: token kind mismatch", r->conn); send_error(r->conn, DW2_ERR_AUTH); return; }
        snprintf(c->player_id, sizeof c->player_id, "%s", r->id.player_id);
        if (c->kind == DW2_KIND_HUMAN && r->id.display_name[0]) snprintf(c->name, sizeof c->name, "%.16s", r->id.display_name);
        become_ready(r->conn);
    } else if (r->rc == -1 && opt_fail_open) {
        fprintf(stderr, "dw2_server: IDUNA unreachable; --auth-fail-open admits %s unverified (no stats)\n", c->name);
        become_ready(r->conn);
    } else {
        vlog("conn %d: IDUNA %s", r->conn, r->rc == -1 ? "unreachable (failing closed)" : "rejected the token");
        send_error(r->conn, DW2_ERR_AUTH);
    }
}

static void drain_results(void) {
#ifdef DW2_HAVE_WORKER
    char b[64]; while (read(wake_fd[0], b, sizeof b) > 0) {}
    for (;;) {
        VResult r; int have = 0;
        pthread_mutex_lock(&jm);
        if (rq_n > 0) { r = rq[0]; memmove(rq, rq + 1, (size_t)(rq_n - 1) * sizeof rq[0]); rq_n--; have = 1; }
        pthread_mutex_unlock(&jm);
        if (!have) break;
        verify_done(&r);
    }
#endif
}

static void enter_queue(int ci) {
    Conn *c = &conns[ci]; Dw2WireMsg r; memset(&r, 0, sizeof r);
    c->state = S_QUEUED; c->queued_seq = ++queue_seq;
    r.type = DW2_S_QUEUED; r.u.queued.waiting = (uint16_t)queued_count(); send_msg(ci, &r);
    try_pair();
}

static void handle_place(int ci, const Dw2WireMsg *m) {
    Conn *c = &conns[ci];
    Dw2WireMsg r; memset(&r, 0, sizeof r);
    if (c->state != S_PACKING) { r.type = DW2_S_PLACE_REJECT; r.u.place_reject.reason = DW2_PL_REJ_BAD_STATE; send_msg(ci, &r); return; }
    Match *mt = &matches[c->match];
    int idx = dw2_ship_place(&mt->ship[c->seat], m->u.place.item_id, m->u.place.anchor_row, m->u.place.anchor_col, m->u.place.rotation);
    if (idx < 0) { r.type = DW2_S_PLACE_REJECT; r.u.place_reject.reason = DW2_PL_REJ_ILLEGAL; send_msg(ci, &r); return; }
    r.type = DW2_S_PLACE_ACK; r.u.place_ack.placement_idx = (uint8_t)idx; send_msg(ci, &r);
}

static void handle_cut(int ci, const Dw2WireMsg *m) {
    Conn *c = &conns[ci];
    Dw2WireMsg r; memset(&r, 0, sizeof r);
    if (c->state != S_PACKING && c->state != S_COMBAT) { r.type = DW2_S_CUT_REJECT; r.u.cut_reject.reason = DW2_CUT_REJ_BAD_STATE; send_msg(ci, &r); return; }
    Match *mt = &matches[c->match];
    int ok = dw2_ship_panic_cut(&mt->ship[c->seat], m->u.cut.placement_idx);
    if (!ok) { r.type = DW2_S_CUT_REJECT; r.u.cut_reject.reason = DW2_CUT_REJ_ILLEGAL; send_msg(ci, &r); return; }
    r.type = DW2_S_CUT_ACK; r.u.cut_ack.placement_idx = m->u.cut.placement_idx; send_msg(ci, &r);
}

static void handle_msg(int ci, const Dw2WireMsg *m) {
    Conn *c = &conns[ci];
    Dw2WireMsg r; memset(&r, 0, sizeof r);
    switch (m->type) {
    case DW2_C_PING: r.type = DW2_S_PONG; r.u.ping.nonce = m->u.ping.nonce; send_msg(ci, &r); return;
    case DW2_C_HELLO:
        if (c->state != S_CONNECTED) { send_error(ci, DW2_ERR_BAD_STATE); return; }
        if (m->u.hello.proto != DW2_PROTO_VERSION) { send_error(ci, DW2_ERR_BAD_PROTO); return; }
        if (m->u.hello.kind > DW2_KIND_BOT) { send_error(ci, DW2_ERR_BAD_FRAME); return; }
        memcpy(c->name, m->u.hello.name, sizeof c->name); c->kind = m->u.hello.kind;
        for (char *q = c->name; *q; q++) if ((unsigned char)*q < 32 || *q == '"' || *q == '\\' || *q == 127) *q = '_';
        c->session_id = next_session++;
        if (opt_noauth) { become_ready(ci); return; }
        if (m->u.hello.token_len > 0) { start_verify(ci, m->u.hello.token, m->u.hello.token_len); return; }
        c->state = S_NEEDAUTH;
        return;
    case DW2_C_AUTH:
        if (c->state == S_NEEDAUTH) { start_verify(ci, m->u.auth.token, m->u.auth.token_len); return; }
        send_error(ci, DW2_ERR_BAD_STATE);
        return;
    case DW2_C_QUEUE:
        if (c->state != S_READY) { send_error(ci, DW2_ERR_BAD_STATE); return; }
        enter_queue(ci);
        return;
    case DW2_C_PLACE: handle_place(ci, m); return;
    case DW2_C_PANIC_CUT: handle_cut(ci, m); return;
    case DW2_C_READY:
        if (c->state != S_PACKING) { send_error(ci, DW2_ERR_BAD_STATE); return; }
        matches[c->match].ready[c->seat] = 1;
        start_combat_if_ready(c->match);
        return;
    case DW2_C_LEAVE:
        if (c->state == S_CONNECTED || c->state == S_NEEDAUTH || c->state == S_VERIFYING) { send_error(ci, DW2_ERR_BAD_STATE); return; }
        leave_match_or_queue(ci);
        return;
    default: send_error(ci, DW2_ERR_BAD_STATE); return;
    }
}

static void read_conn(int ci) {
    Conn *c = &conns[ci];
    long n = dw2_recv(c->fd, c->in + c->in_n, INBUF - c->in_n);
    if (n == 0) { conn_close(ci); return; }
    if (n < 0) { if (!dw2_wouldblock()) conn_close(ci); return; }
    c->in_n += (size_t)n;
    size_t off = 0;
    while (off < c->in_n && !c->close_after_flush) {
        Dw2WireMsg m; size_t used = 0;
        int r = dw2_decode(c->in + off, c->in_n - off, &m, &used);
        if (r == 0) break;
        if (r < 0) { send_error(ci, DW2_ERR_BAD_FRAME); break; }
        off += used;
        handle_msg(ci, &m);
        if (conns[ci].state == S_FREE) return;
    }
    if (off) { memmove(c->in, c->in + off, c->in_n - off); c->in_n -= off; }
    if (c->in_n >= INBUF && !c->close_after_flush) send_error(ci, DW2_ERR_BAD_FRAME);
}

static void accept_conns(dw2_sock ls) {
    for (;;) {
        dw2_sock fd = accept(ls, NULL, NULL);
        if (fd == DW2_BAD_SOCK) return;
        int slot = -1;
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state == S_FREE) { slot = i; break; }
        if (slot < 0) { dw2_close(fd); continue; }
        dw2_nonblock(fd);
        int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
        Conn *c = &conns[slot]; memset(c, 0, sizeof *c);
        c->fd = fd; c->state = S_CONNECTED; c->match = -1; c->connect_ms = dw2_now_ms();
    }
}

static void expire_timers(void) {
    uint64_t now = dw2_now_ms();
    for (int i = 0; i < MAX_CONNS; i++)
        if ((conns[i].state == S_CONNECTED || conns[i].state == S_NEEDAUTH || conns[i].state == S_VERIFYING) && now - conns[i].connect_ms > HELLO_TIMEOUT_MS) conn_close(i);
    for (int i = 0; i < MAX_MATCHES; i++) {
        Match *mt = &matches[i];
        if (!mt->active) continue;
        if (!mt->combat_started) { start_combat_if_ready(i); continue; }
        if (now >= mt->next_tick_deadline) run_tick(i);
    }
}

int main(int argc, char **argv) {
    int port = 7800; const char *bind_addr = "0.0.0.0", *iduna_url = NULL, *secret_file = NULL, *agent_name = "D2-SERVER";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) { printf("dw2_server %s\n", DW2_VERSION); return 0; }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bind") && i + 1 < argc) bind_addr = argv[++i];
        else if (!strcmp(argv[i], "--fast-forward")) opt_fast_forward = 1;
        else if (!strcmp(argv[i], "--no-auth")) { opt_noauth = 1; opt_noauth_explicit = 1; }
        else if (!strcmp(argv[i], "--iduna-url") && i + 1 < argc) iduna_url = argv[++i];
        else if (!strcmp(argv[i], "--agent-secret-file") && i + 1 < argc) secret_file = argv[++i];
        else if (!strcmp(argv[i], "--agent-name") && i + 1 < argc) agent_name = argv[++i];
        else if (!strcmp(argv[i], "--auth-fail-open")) opt_fail_open = 1;
        else if (!strcmp(argv[i], "--pack-ms") && i + 1 < argc) { opt_pack_ms = atoi(argv[++i]); if (opt_pack_ms < 1000 || opt_pack_ms > 300000) { fprintf(stderr, "--pack-ms must be 1000..300000\n"); return 2; } }
        else if (!strcmp(argv[i], "--tick-ms") && i + 1 < argc) { opt_tick_ms = atoi(argv[++i]); if (opt_tick_ms < 10 || opt_tick_ms > 10000) { fprintf(stderr, "--tick-ms must be 10..10000\n"); return 2; } }
        else if (!strcmp(argv[i], "--verbose")) opt_verbose = 1;
        else {
            fprintf(stderr, "usage: dw2_server [--port N] [--bind ADDR] [--fast-forward] [--no-auth] [--iduna-url URL --agent-secret-file F [--agent-name N] [--auth-fail-open]] [--pack-ms N] [--tick-ms N] [--verbose] [--version]\n");
            return 2;
        }
    }
    if (iduna_url) {
#ifndef DW2_HAVE_WORKER
        fprintf(stderr, "dw2_server: --iduna-url is not supported in this (Windows) build; use --no-auth\n"); return 2;
#endif
        int rc = dwi2_configure(&iduna, iduna_url, agent_name, secret_file);
        if (rc == -1) { fprintf(stderr, "dw2_server: bad --iduna-url %s\n", iduna_url); return 2; }
        if (rc != 0) { fprintf(stderr, "dw2_server: cannot read agent secret for %s from %s (rc %d)\n", agent_name, secret_file ? secret_file : "(no --agent-secret-file)", rc); return 1; }
        opt_iduna = 1;
        if (!opt_noauth_explicit) opt_noauth = 0;
    }
    if (dw2_net_init() != 0) { fprintf(stderr, "dw2_server: net init failed\n"); return 1; }
    if (opt_iduna && start_worker() != 0) { fprintf(stderr, "dw2_server: cannot start IDUNA worker\n"); return 1; }
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    for (int i = 0; i < MAX_CONNS; i++) { conns[i].fd = DW2_BAD_SOCK; conns[i].match = -1; }
    dw2_sock ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == DW2_BAD_SOCK) { fprintf(stderr, "dw2_server: socket failed\n"); return 1; }
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, bind_addr, &sa.sin_addr) != 1) { fprintf(stderr, "dw2_server: bad --bind %s\n", bind_addr); return 2; }
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 64) != 0) { fprintf(stderr, "dw2_server: bind/listen on %s:%d failed\n", bind_addr, port); return 1; }
    if (port == 0) { socklen_t sl = sizeof sa; getsockname(ls, (struct sockaddr *)&sa, &sl); port = ntohs(sa.sin_port); }
    dw2_nonblock(ls);
    seed_state ^= (uint32_t)dw2_now_ms() | 1u;
    printf("dw2_server %s listening on %s:%d%s%s\n", DW2_VERSION, bind_addr, port, opt_fast_forward ? " (fast-forward)" : "", opt_noauth ? " (no-auth)" : " (IDUNA auth required)"); fflush(stdout);

    while (!stop_flag) {
        struct pollfd pf[MAX_CONNS + 2]; int map[MAX_CONNS + 2]; unsigned n = 0;
        pf[n].fd = ls; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = -1;
#ifdef DW2_HAVE_WORKER
        if (opt_iduna) { pf[n].fd = wake_fd[0]; pf[n].events = POLLIN; pf[n].revents = 0; map[n++] = -2; }
#endif
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) {
            pf[n].fd = conns[i].fd; pf[n].events = (short)(POLLIN | (conns[i].out_n ? POLLOUT : 0)); pf[n].revents = 0; map[n++] = i;
        }
        int rc = dw2_poll(pf, n, opt_fast_forward ? 10 : 200);
        if (rc > 0) {
            for (unsigned k = 0; k < n; k++) {
                if (!pf[k].revents) continue;
                if (map[k] == -1) { accept_conns(ls); continue; }
                if (map[k] == -2) { drain_results(); continue; }
                int ci = map[k];
                if (conns[ci].state == S_FREE) continue;
                if (pf[k].revents & POLLOUT) flush_conn(ci);
                if (pf[k].revents & (POLLIN | POLLHUP | POLLERR)) read_conn(ci);
            }
        }
        for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE && conns[i].close_after_flush) {
            flush_conn(i); conn_close(i);
        }
        expire_timers();
        if (opt_iduna) drain_results();
    }
    for (int i = 0; i < MAX_MATCHES; i++) if (matches[i].active) end_match(i, DW2_END_SERVER);
    for (int i = 0; i < MAX_CONNS; i++) if (conns[i].state != S_FREE) { flush_conn(i); conn_close(i); }
    dw2_close(ls);
    stop_worker();
    fprintf(stderr, "dw2_server: shutdown matches=%ld forfeits=%ld\n", st_matches, st_forfeits);
    return 0;
}
