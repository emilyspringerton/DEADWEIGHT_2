/* dw2_test_client -- headless scripted client for verifying dw2_server end to end over the real
 * TCP wire protocol (core/protocol.h). Not a real client (that's D3's job) -- this is the same
 * "prove the server actually works, not just that it compiles" discipline DEADWEIGHT_2's own
 * apps/local/main.c --selftest already established for the local core loop.
 *
 * Loadout is scripted via --place ITEM,ROW,COL,ROT (repeatable) and --cut IDX (repeatable, applied
 * after all --place args). Prints every server message to stdout and exits 0 once MATCH_END
 * arrives (nonzero on any protocol/connection error), so two instances racing against the same
 * server can be scripted from a shell test and their exit codes/stdout diffed against hand-derived
 * expectations. */
#include "../core/net.h"
#include <stdlib.h>
#include "../core/protocol.h"

#define MAX_PLACES 16

typedef struct { int item, row, col, rot; } PlaceArg;

static dw2_sock sock;
static uint8_t inbuf[4096]; size_t inbuf_n = 0;

static void send_msg(const Dw2WireMsg *m) {
    uint8_t b[DW2_MAX_FRAME_LEN + 2];
    int n = dw2_encode(m, b, sizeof b);
    if (n < 0) { fprintf(stderr, "encode failed for type %d\n", m->type); exit(1); }
    long off = 0;
    while (off < n) { long w = dw2_send(sock, b + off, (size_t)(n - off)); if (w <= 0) { fprintf(stderr, "send failed\n"); exit(1); } off += w; }
}

/* Blocking read of exactly one decoded frame (loops recv() until dw2_decode succeeds). */
static int recv_msg(Dw2WireMsg *out, int timeout_ms) {
    for (;;) {
        size_t used = 0;
        int r = dw2_decode(inbuf, inbuf_n, out, &used);
        if (r == 1) { memmove(inbuf, inbuf + used, inbuf_n - used); inbuf_n -= used; return 1; }
        if (r < 0) { fprintf(stderr, "malformed frame from server\n"); return -1; }
        struct pollfd p = { sock, POLLIN, 0 };
        int pr = dw2_poll(&p, 1, timeout_ms);
        if (pr <= 0) { fprintf(stderr, "timeout waiting for server message\n"); return -1; }
        long n = dw2_recv(sock, inbuf + inbuf_n, sizeof inbuf - inbuf_n);
        if (n <= 0) { fprintf(stderr, "connection closed\n"); return -1; }
        inbuf_n += (size_t)n;
    }
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1"; int port = 7800; const char *name = "tester";
    PlaceArg places[MAX_PLACES]; int place_n = 0;
    int cuts[MAX_PLACES]; int cut_n = 0;
    int timeout_ms = 20000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) timeout_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--place") && i + 1 < argc) {
            if (place_n >= MAX_PLACES) { fprintf(stderr, "too many --place args\n"); return 2; }
            PlaceArg *p = &places[place_n++];
            if (sscanf(argv[++i], "%d,%d,%d,%d", &p->item, &p->row, &p->col, &p->rot) != 4) { fprintf(stderr, "bad --place %s (want ITEM,ROW,COL,ROT)\n", argv[i]); return 2; }
        } else if (!strcmp(argv[i], "--cut") && i + 1 < argc) {
            if (cut_n >= MAX_PLACES) { fprintf(stderr, "too many --cut args\n"); return 2; }
            cuts[cut_n++] = atoi(argv[++i]);
        } else { fprintf(stderr, "usage: dw2_test_client --host H --port N --name NAME [--place ITEM,ROW,COL,ROT ...] [--cut IDX ...] [--timeout-ms N]\n"); return 2; }
    }

    if (dw2_net_init() != 0) { fprintf(stderr, "net init failed\n"); return 1; }
    sock = dw2_connect(host, port);
    if (sock == DW2_BAD_SOCK) { fprintf(stderr, "connect to %s:%d failed\n", host, port); return 1; }

    Dw2WireMsg m; memset(&m, 0, sizeof m);
    m.type = DW2_C_HELLO; m.u.hello.proto = DW2_PROTO_VERSION; m.u.hello.kind = DW2_KIND_HUMAN;
    snprintf(m.u.hello.name, sizeof m.u.hello.name, "%s", name);
    send_msg(&m);
    if (recv_msg(&m, timeout_ms) != 1 || m.type != DW2_S_WELCOME) { fprintf(stderr, "expected WELCOME\n"); return 1; }
    printf("WELCOME session=%u\n", m.u.welcome.session_id);

    memset(&m, 0, sizeof m); m.type = DW2_C_QUEUE; send_msg(&m);
    if (recv_msg(&m, timeout_ms) != 1 || m.type != DW2_S_QUEUED) { fprintf(stderr, "expected QUEUED\n"); return 1; }
    printf("QUEUED waiting=%u\n", m.u.queued.waiting);

    if (recv_msg(&m, timeout_ms) != 1 || m.type != DW2_S_MATCH_FOUND) { fprintf(stderr, "expected MATCH_FOUND\n"); return 1; }
    printf("MATCH_FOUND id=%u seat=%u opp=%s pack_ms=%u\n", m.u.match_found.match_id, m.u.match_found.seat, m.u.match_found.opp_name, m.u.match_found.pack_ms);
    uint32_t match_id = m.u.match_found.match_id;

    for (int i = 0; i < place_n; i++) {
        memset(&m, 0, sizeof m); m.type = DW2_C_PLACE;
        m.u.place.item_id = (uint8_t)places[i].item; m.u.place.anchor_row = (uint8_t)places[i].row;
        m.u.place.anchor_col = (uint8_t)places[i].col; m.u.place.rotation = (uint8_t)places[i].rot;
        send_msg(&m);
        if (recv_msg(&m, timeout_ms) != 1) return 1;
        if (m.type == DW2_S_PLACE_ACK) printf("PLACE_ACK idx=%u\n", m.u.place_ack.placement_idx);
        else if (m.type == DW2_S_PLACE_REJECT) printf("PLACE_REJECT reason=%u\n", m.u.place_reject.reason);
        else { fprintf(stderr, "unexpected reply to PLACE: type %d\n", m.type); return 1; }
    }
    for (int i = 0; i < cut_n; i++) {
        memset(&m, 0, sizeof m); m.type = DW2_C_PANIC_CUT; m.u.cut.placement_idx = (uint8_t)cuts[i];
        send_msg(&m);
        if (recv_msg(&m, timeout_ms) != 1) return 1;
        if (m.type == DW2_S_CUT_ACK) printf("CUT_ACK idx=%u\n", m.u.cut_ack.placement_idx);
        else if (m.type == DW2_S_CUT_REJECT) printf("CUT_REJECT reason=%u\n", m.u.cut_reject.reason);
        else { fprintf(stderr, "unexpected reply to PANIC_CUT: type %d\n", m.type); return 1; }
    }

    memset(&m, 0, sizeof m); m.type = DW2_C_READY; send_msg(&m);

    for (;;) {
        if (recv_msg(&m, timeout_ms) != 1) return 1;
        if (m.type == DW2_S_COMBAT_START) {
            printf("COMBAT_START hull=%u/%u armor=%u/%u cargo=%u/%u\n", m.u.combat_start.hull_you, m.u.combat_start.hull_opp,
                   m.u.combat_start.armor_you, m.u.combat_start.armor_opp, m.u.combat_start.cargo_you, m.u.combat_start.cargo_opp);
        } else if (m.type == DW2_S_TICK) {
            printf("TICK t=%u hull=%u/%u armor=%u/%u shatter=%u/%u\n", m.u.tick.elapsed_ticks, m.u.tick.hull_you, m.u.tick.hull_opp,
                   m.u.tick.armor_you, m.u.tick.armor_opp, m.u.tick.shatter_you, m.u.tick.shatter_opp);
        } else if (m.type == DW2_S_MATCH_END) {
            if (m.u.match_end.match_id != match_id) { fprintf(stderr, "MATCH_END for wrong match_id\n"); return 1; }
            printf("MATCH_END result=%u reason=%u ticks=%u\n", m.u.match_end.result, m.u.match_end.reason, m.u.match_end.ticks);
            break;
        } else { fprintf(stderr, "unexpected message during combat: type %d\n", m.type); return 1; }
    }

    dw2_close(sock);
    return 0;
}
