/* dw2_client -- D2 (folded, was scoped as a separate D3): a real, interactive client for dw2_server, speaking
 * core/protocol.h's actual wire protocol (not tools/dw2_test_client.c's scripted test harness,
 * which stays exactly what it always was -- a headless protocol-edge-case test tool, not a real
 * client). Connect -> auto-queue -> pack against a live opponent within the server's time limit
 * -> real-time combat driven by the server's own tick clock -> see the result.
 *
 * Real, named simplification (not an oversight): core/protocol.h's COMBAT_START/TICK messages
 * only ever carry hull/armor/cargo/shatter SCALARS for each side -- never grid/placement detail,
 * for either ship. That's deliberately correct (an opponent's live grid is hidden information,
 * and a client can't run dw2_ship_tick locally at all -- it takes both ships' full state, and
 * this client never has the opponent's). So: during PACKING this client renders its own grid in
 * full (mirrored locally via the same core/combat.h calls dw2_server itself runs -- placement
 * legality is a pure deterministic function of the shared grid state, so local prediction and the
 * server's own authoritative check always agree) plus an opponent placeholder + pack-timer bar;
 * during COMBAT it renders both sides as hull/armor HUD bars only, no grid. Panic Cut mid-combat
 * (legal per core/combat.h's own doc comment) is addressed by placement index (0-9, printed to
 * the console as a legend at COMBAT_START) rather than by clicking a cell, since no live grid
 * exists to click during combat. A richer mid-combat protocol (per-cell deltas) is real, deferred
 * future work, not this client's job to invent.
 *
 * Auth: obtains a token (if any) BEFORE opening the game socket -- via --token directly, or a
 * guest register/login against --iduna-url (core/iduna.h, persisted across runs with
 * --guest-file so relaunching resumes the same guest identity instead of minting a new one every
 * time, matching the mobile-game guest-account convention DEADWEIGHT/NORTHSTAR.md named for this
 * whole game). HELLO always goes out with an empty token (its own inline token field is only 200
 * bytes -- too small for a real ~400-500 byte ES256 JWT, per protocol.h's own DW2_MAX_AUTH_TOKEN
 * comment); a real token is sent as a separate AUTH message right behind it, matching
 * apps/server/main.c's S_NEEDAUTH->AUTH path. No token obtained: works as-is against a --no-auth
 * server (the local-dev/CI convention scripts/build.sh already uses for D2).
 *
 * --selftest: like apps/local/main.c's own --selftest, runs a full match through the exact same
 * code paths as interactive play (try_place/try_cut/send_ready), just auto-scripted instead of
 * keyboard-driven, through the real SDL render path (SDL_VIDEODRIVER=dummy) -- headless, but not
 * a shortcut. --empty-grid (selftest only) skips placement entirely, for a two-instance win/loss
 * smoke test mirroring tools/dw2_test_client.c's own A/B pattern (see scripts/build.sh). */
#include "../../core/net.h"      /* must come before any other system header, incl. SDL.h -- own doc comment */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/protocol.h"
#include "../../core/combat.h"
#include "../../core/items.h"
#include "../../core/iduna.h"

#define CELL_PX 64
#define GRID_ORIGIN_X 40
#define GRID_ORIGIN_Y 60
#define WIN_W 900
#define WIN_H 560
#define OPP_ORIGIN_X (GRID_ORIGIN_X + DW2_GRID_W * CELL_PX + 60)

typedef enum { CS_CONNECTING, CS_QUEUED, CS_PACKING, CS_COMBAT, CS_DONE, CS_ERROR } ClientState;

typedef struct { Uint8 r, g, b; } Col;
static const Col COL_BG = { 18, 20, 28 }, COL_CURSOR = { 255, 255, 255 },
    COL_RUINED = { 90, 30, 30 }, COL_BLOCKADE = { 10, 10, 10 }, COL_PANEL = { 40, 42, 55 };
static const Col COL_KIND[3] = { { 215, 70, 60 }, { 225, 160, 40 }, { 70, 140, 230 } };
static const Col COL_GENERATOR = { 80, 220, 120 }, COL_CONDUCTOR = { 200, 200, 210 },
    COL_SPLITTER = { 80, 210, 220 }, COL_WEAPON = { 240, 90, 40 }, COL_ARMOR = { 90, 140, 240 },
    COL_DEAD = { 70, 70, 70 }, COL_HULL_BAR = { 220, 60, 60 }, COL_TIMER = { 230, 200, 60 };

static SDL_Renderer *R;

static void rect(int x, int y, int w, int h, Col c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, 255);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(R, &rc);
}
static void frame(int x, int y, int w, int h, Col c, int t) {
    rect(x, y, w, t, c); rect(x, y + h - t, w, t, c); rect(x, y, t, h, c); rect(x + w - t, y, t, h, c);
}

static Col role_color(int role, int kind) {
    switch (role) {
        case DW2_ROLE_GENERATOR: return COL_GENERATOR;
        case DW2_ROLE_CONDUCTOR: return COL_CONDUCTOR;
        case DW2_ROLE_SPLITTER: return COL_SPLITTER;
        case DW2_ROLE_WEAPON: return COL_WEAPON;
        case DW2_ROLE_ARMOR: return COL_ARMOR;
        case DW2_ROLE_DEAD: return COL_DEAD;
        default: return COL_KIND[kind];
    }
}

static void draw_ship(const Dw2Ship *s, int origin_x, int origin_y, int cursor_row, int cursor_col, int show_cursor) {
    for (int row = 0; row < DW2_GRID_H; row++) {
        for (int col = 0; col < DW2_GRID_W; col++) {
            int cell = dw2_cell(row, col);
            int x = origin_x + col * CELL_PX, y = origin_y + row * CELL_PX;
            Col bg = COL_BG;
            if (s->grid.blockade & dw2_bit(cell)) bg = COL_BLOCKADE;
            else if (s->grid.ruined & dw2_bit(cell)) bg = COL_RUINED;
            rect(x, y, CELL_PX - 2, CELL_PX - 2, bg);
        }
    }
    for (int p = 0; p < s->placement_n; p++) {
        const Dw2Placement *pl = &s->placements[p];
        if (pl->item_id < 0) continue;
        int kind = dw2_catalog[pl->item_id].kind;
        for (int c = 0; c < pl->cell_n; c++) {
            if (!pl->cells[c].in_bounds) continue;
            int x = origin_x + pl->cells[c].col * CELL_PX, y = origin_y + pl->cells[c].row * CELL_PX;
            rect(x, y, CELL_PX - 2, CELL_PX - 2, role_color(pl->cells[c].role, kind));
        }
    }
    if (show_cursor) {
        int x = origin_x + cursor_col * CELL_PX, y = origin_y + cursor_row * CELL_PX;
        frame(x, y, CELL_PX - 2, CELL_PX - 2, COL_CURSOR, 3);
    }
}

static void draw_hud_bars(int origin_x, int origin_y, float hull_pct, float armor) {
    int bar_w = DW2_GRID_W * CELL_PX - 2;
    float hull_frac = hull_pct / 100.0f; if (hull_frac < 0) hull_frac = 0; if (hull_frac > 1) hull_frac = 1;
    rect(origin_x, origin_y, bar_w, 14, (Col){ 40, 20, 20 });
    rect(origin_x, origin_y, (int)(bar_w * hull_frac), 14, COL_HULL_BAR);
    float armor_frac = armor / 100.0f; if (armor_frac > 1) armor_frac = 1; if (armor_frac < 0) armor_frac = 0;
    rect(origin_x, origin_y + 20, bar_w, 10, (Col){ 20, 30, 45 });
    rect(origin_x, origin_y + 20, (int)(bar_w * armor_frac), 10, COL_ARMOR);
}

static void find_placement_under_cursor(const Dw2Ship *s, int row, int col, int *out_idx) {
    *out_idx = -1;
    dw2_mask bit = dw2_bit(dw2_cell(row, col));
    for (int p = 0; p < s->placement_n; p++)
        if (s->placements[p].item_id >= 0 && (s->placements[p].mask & bit)) { *out_idx = p; return; }
}

/* --- networking --- */

static dw2_sock sock;
static uint8_t inbuf[4096]; static size_t inbuf_n = 0;

static void send_msg(const Dw2WireMsg *m) {
    uint8_t b[DW2_MAX_FRAME_LEN + 2];
    int n = dw2_encode(m, b, sizeof b);
    if (n < 0) { fprintf(stderr, "encode failed for type %d\n", m->type); exit(1); }
    long off = 0;
    while (off < n) { long w = dw2_send(sock, b + off, (size_t)(n - off)); if (w <= 0) { fprintf(stderr, "send failed\n"); exit(1); } off += w; }
}

/* Blocking read of exactly one decoded frame -- used only for the initial handshake, before the
 * socket switches to non-blocking for the async interactive/selftest loop. `quiet`: don't print on
 * timeout -- used for the short auth-required probe below, where timing out is the expected,
 * successful outcome (means "go ahead and send AUTH"), not a real error. */
static int recv_msg_blocking(Dw2WireMsg *out, int timeout_ms, int quiet) {
    for (;;) {
        size_t used = 0;
        int r = dw2_decode(inbuf, inbuf_n, out, &used);
        if (r == 1) { memmove(inbuf, inbuf + used, inbuf_n - used); inbuf_n -= used; return 1; }
        if (r < 0) { fprintf(stderr, "malformed frame from server\n"); return -1; }
        struct pollfd p = { sock, POLLIN, 0 };
        int pr = dw2_poll(&p, 1, timeout_ms);
        if (pr <= 0) { if (!quiet) fprintf(stderr, "timeout waiting for server\n"); return -1; }
        long n = dw2_recv(sock, inbuf + inbuf_n, sizeof inbuf - inbuf_n);
        if (n <= 0) { fprintf(stderr, "connection closed\n"); return -1; }
        inbuf_n += (size_t)n;
    }
}

/* --- game state --- */

static ClientState state = CS_CONNECTING;
static int running = 1;
static Dw2Ship player;
static int cursor_row = 0, cursor_col = 0, sel_item = 0, sel_rotation = 0;
static int ready_sent = 0;
static char opp_name[DW2_NAME_LEN + 1] = "";
static uint32_t match_id = 0; static uint32_t pack_ms = 0; static uint64_t pack_deadline_ms = 0;
static float hull_you = 100, hull_opp = 100, armor_you = 0, armor_opp = 0;
static uint16_t waiting_count = 0;
static uint32_t match_ticks = 0; static uint8_t match_result = 0, match_reason = 0;

static int selftest_mode = 0, selftest_empty = 0;

static void try_place(int item, int row, int col, int rot) {
    int idx = dw2_ship_place(&player, item, row, col, rot);
    if (idx < 0) { printf("illegal placement: %s at (%d,%d)\n", dw2_catalog[item].name, row, col); return; }
    printf("placed %s at (%d,%d) -> idx %d\n", dw2_catalog[item].name, row, col, idx);
    Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_C_PLACE;
    m.u.place.item_id = (uint8_t)item; m.u.place.anchor_row = (uint8_t)row;
    m.u.place.anchor_col = (uint8_t)col; m.u.place.rotation = (uint8_t)rot;
    send_msg(&m);
}
static void try_cut(int placement_idx) {
    if (placement_idx < 0) return;
    int ok = dw2_ship_panic_cut(&player, placement_idx);
    if (!ok) { printf("Panic Cut failed (idx %d, no room for fragments or already cut)\n", placement_idx); return; }
    printf("Panic Cut! idx %d fragmented.\n", placement_idx);
    Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_C_PANIC_CUT; m.u.cut.placement_idx = (uint8_t)placement_idx;
    send_msg(&m);
}
static void send_ready(void) {
    if (ready_sent) return;
    ready_sent = 1;
    Dw2WireMsg m; memset(&m, 0, sizeof m); m.type = DW2_C_READY; send_msg(&m);
    printf("READY sent.\n");
}

static void handle_server_msg(const Dw2WireMsg *m) {
    switch (m->type) {
    case DW2_S_QUEUED:
        waiting_count = m->u.queued.waiting;
        printf("QUEUED waiting=%u\n", waiting_count);
        break;
    case DW2_S_MATCH_FOUND:
        match_id = m->u.match_found.match_id;
        snprintf(opp_name, sizeof opp_name, "%s", m->u.match_found.opp_name);
        pack_ms = m->u.match_found.pack_ms;
        pack_deadline_ms = dw2_now_ms() + pack_ms;
        dw2_ship_init(&player);
        cursor_row = cursor_col = sel_item = sel_rotation = 0; ready_sent = 0;
        printf("MATCH_FOUND id=%u seat=%u opp=%s(%s) pack_ms=%u\n", match_id, m->u.match_found.seat,
               opp_name, m->u.match_found.opp_kind == DW2_KIND_BOT ? "bot" : "human", pack_ms);
        state = CS_PACKING;
        if (selftest_mode) {
            if (!selftest_empty) {
                try_place(DW2_ITEM_GENERATOR, 0, 0, 0);
                try_place(DW2_ITEM_CONDUCTOR, 0, 1, 0);
                try_place(DW2_ITEM_RAILGUN, 0, 2, 0);
                try_place(DW2_ITEM_BULWARK, 3, 3, 0);
            }
            send_ready();
        }
        break;
    case DW2_S_PLACE_ACK: printf("PLACE_ACK idx=%u\n", m->u.place_ack.placement_idx); break;
    case DW2_S_PLACE_REJECT: printf("PLACE_REJECT reason=%u\n", m->u.place_reject.reason); break;
    case DW2_S_CUT_ACK: printf("CUT_ACK idx=%u\n", m->u.cut_ack.placement_idx); break;
    case DW2_S_CUT_REJECT: printf("CUT_REJECT reason=%u\n", m->u.cut_reject.reason); break;
    case DW2_S_COMBAT_START:
        hull_you = m->u.combat_start.hull_you; hull_opp = m->u.combat_start.hull_opp;
        armor_you = m->u.combat_start.armor_you; armor_opp = m->u.combat_start.armor_opp;
        printf("COMBAT_START hull=%u/%u armor=%u/%u cargo=%u/%u\n", m->u.combat_start.hull_you, m->u.combat_start.hull_opp,
               m->u.combat_start.armor_you, m->u.combat_start.armor_opp, m->u.combat_start.cargo_you, m->u.combat_start.cargo_opp);
        printf("your placements (Panic Cut by index with 0-9 during combat):\n");
        for (int i = 0; i < player.placement_n; i++)
            if (player.placements[i].item_id >= 0) printf("  [%d] %s\n", i, dw2_catalog[player.placements[i].item_id].name);
        state = CS_COMBAT;
        break;
    case DW2_S_TICK:
        hull_you = m->u.tick.hull_you; hull_opp = m->u.tick.hull_opp;
        armor_you = m->u.tick.armor_you; armor_opp = m->u.tick.armor_opp;
        printf("TICK t=%u hull=%u/%u armor=%u/%u shatter=%u/%u\n", m->u.tick.elapsed_ticks, m->u.tick.hull_you, m->u.tick.hull_opp,
               m->u.tick.armor_you, m->u.tick.armor_opp, m->u.tick.shatter_you, m->u.tick.shatter_opp);
        break;
    case DW2_S_MATCH_END:
        match_result = m->u.match_end.result; match_reason = m->u.match_end.reason; match_ticks = m->u.match_end.ticks;
        printf("MATCH_END result=%u reason=%u ticks=%u\n", match_result, match_reason, match_ticks);
        state = CS_DONE;
        break;
    case DW2_S_ERROR:
        printf("ERROR code=%u\n", m->u.error.code);
        if (state == CS_CONNECTING || state == CS_QUEUED) state = CS_ERROR;
        break;
    case DW2_S_PONG: default: break;
    }
}

static void poll_incoming(void) {
    for (;;) {
        long n = dw2_recv(sock, inbuf + inbuf_n, sizeof inbuf - inbuf_n);
        if (n > 0) { inbuf_n += (size_t)n; continue; }
        if (n == 0) { fprintf(stderr, "server closed connection\n"); state = CS_ERROR; return; }
        if (!dw2_wouldblock()) { fprintf(stderr, "recv error\n"); state = CS_ERROR; return; }
        break;
    }
    for (;;) {
        Dw2WireMsg m; size_t used = 0;
        int r = dw2_decode(inbuf, inbuf_n, &m, &used);
        if (r == 0) break;
        if (r < 0) { fprintf(stderr, "malformed frame from server\n"); state = CS_ERROR; return; }
        memmove(inbuf, inbuf + used, inbuf_n - used); inbuf_n -= used;
        handle_server_msg(&m);
    }
}

static void handle_key(SDL_Keycode k) {
    if (k == SDLK_ESCAPE) { running = 0; return; }
    if (state == CS_PACKING && !ready_sent) {
        if (k >= SDLK_1 && k <= SDLK_7) { sel_item = (int)(k - SDLK_1); printf("selected: %s\n", dw2_catalog[sel_item].name); }
        else if (k == SDLK_UP || k == SDLK_w) cursor_row = cursor_row > 0 ? cursor_row - 1 : cursor_row;
        else if (k == SDLK_DOWN || k == SDLK_s) cursor_row = cursor_row < DW2_GRID_H - 1 ? cursor_row + 1 : cursor_row;
        else if (k == SDLK_LEFT || k == SDLK_a) cursor_col = cursor_col > 0 ? cursor_col - 1 : cursor_col;
        else if (k == SDLK_RIGHT || k == SDLK_d) cursor_col = cursor_col < DW2_GRID_W - 1 ? cursor_col + 1 : cursor_col;
        else if (k == SDLK_r) sel_rotation = (sel_rotation + 1) % 4;
        else if (k == SDLK_RETURN) try_place(sel_item, cursor_row, cursor_col, sel_rotation);
        else if (k == SDLK_c) { int idx; find_placement_under_cursor(&player, cursor_row, cursor_col, &idx); try_cut(idx); }
        else if (k == SDLK_f) send_ready();
    } else if (state == CS_COMBAT) {
        if (k >= SDLK_0 && k <= SDLK_9) try_cut((int)(k - SDLK_0));
    }
}

static void render(void) {
    rect(0, 0, WIN_W, WIN_H, COL_BG);
    if (state == CS_PACKING) {
        draw_ship(&player, GRID_ORIGIN_X, GRID_ORIGIN_Y, cursor_row, cursor_col, !ready_sent);
        rect(OPP_ORIGIN_X, GRID_ORIGIN_Y, DW2_GRID_W * CELL_PX - 2, DW2_GRID_H * CELL_PX - 2, COL_PANEL);
        uint64_t now = dw2_now_ms();
        float remain = pack_deadline_ms > now ? (float)(pack_deadline_ms - now) / (float)(pack_ms ? pack_ms : 1) : 0;
        int bar_w = DW2_GRID_W * CELL_PX - 2;
        rect(GRID_ORIGIN_X, GRID_ORIGIN_Y + DW2_GRID_H * CELL_PX + 12, bar_w, 10, (Col){ 40, 35, 15 });
        rect(GRID_ORIGIN_X, GRID_ORIGIN_Y + DW2_GRID_H * CELL_PX + 12, (int)(bar_w * remain), 10, COL_TIMER);
    } else if (state == CS_COMBAT || state == CS_DONE) {
        draw_hud_bars(GRID_ORIGIN_X, GRID_ORIGIN_Y, hull_you, armor_you);
        draw_hud_bars(OPP_ORIGIN_X, GRID_ORIGIN_Y, hull_opp, armor_opp);
    }
    SDL_RenderPresent(R);
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1"; int port = 7800; char name[DW2_NAME_LEN + 1] = "player";
    const char *opt_token = NULL, *opt_iduna_url = NULL, *opt_guest_file = NULL;
    int connect_timeout_ms = 20000, selftest_timeout_ms = 20000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) snprintf(name, sizeof name, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--token") && i + 1 < argc) opt_token = argv[++i];
        else if (!strcmp(argv[i], "--iduna-url") && i + 1 < argc) opt_iduna_url = argv[++i];
        else if (!strcmp(argv[i], "--guest-file") && i + 1 < argc) opt_guest_file = argv[++i];
        else if (!strcmp(argv[i], "--selftest")) selftest_mode = 1;
        else if (!strcmp(argv[i], "--empty-grid")) selftest_empty = 1;
        else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) selftest_timeout_ms = connect_timeout_ms = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: dw2_client --host H --port N --name NAME [--token T | --iduna-url URL [--guest-file F]] "
                             "[--selftest [--empty-grid]] [--timeout-ms N]\n");
            return 2;
        }
    }

    char token[1536] = ""; int have_token = 0;
    if (opt_token) { snprintf(token, sizeof token, "%s", opt_token); have_token = 1; }
    else if (opt_iduna_url) {
        Dw2Iduna id;
        if (dwi2_configure(&id, opt_iduna_url, NULL, NULL) != 0) { fprintf(stderr, "bad --iduna-url\n"); return 1; }
        char pid[64] = "", secret[128] = "", out_name[64] = ""; int status = 0, loaded = 0, rc;
        if (opt_guest_file) {
            FILE *f = fopen(opt_guest_file, "r");
            if (f) { loaded = (fscanf(f, "%63s %127s", pid, secret) == 2); fclose(f); }
        }
        rc = loaded ? dwi2_guest_login(&id, pid, secret, token, sizeof token, out_name, sizeof out_name, &status)
                    : dwi2_guest_register(&id, name[0] ? name : NULL, pid, sizeof pid, secret, sizeof secret,
                                           token, sizeof token, out_name, sizeof out_name, &status);
        if (rc != 0) { fprintf(stderr, "guest %s failed (rc=%d status=%d)\n", loaded ? "login" : "register", rc, status); return 1; }
        if (out_name[0]) snprintf(name, sizeof name, "%.16s", out_name); /* DW2_NAME_LEN cap, matches apps/server/main.c's own convention */
        if (opt_guest_file && !loaded) { FILE *f = fopen(opt_guest_file, "w"); if (f) { fprintf(f, "%s %s\n", pid, secret); fclose(f); } }
        printf("guest %s: player_id=%s name=%s\n", loaded ? "login" : "register", pid, name);
        have_token = 1;
    }

    if (dw2_net_init() != 0) { fprintf(stderr, "net init failed\n"); return 1; }
    sock = dw2_connect(host, port);
    if (sock == DW2_BAD_SOCK) { fprintf(stderr, "connect to %s:%d failed\n", host, port); return 1; }

    Dw2WireMsg m; memset(&m, 0, sizeof m);
    m.type = DW2_C_HELLO; m.u.hello.proto = DW2_PROTO_VERSION; m.u.hello.kind = DW2_KIND_HUMAN;
    snprintf(m.u.hello.name, sizeof m.u.hello.name, "%s", name);
    send_msg(&m);
    /* HELLO's own inline token field is only DW2_MAX_TOKEN=200 bytes -- too small for a real
     * ~400-500 byte ES256 JWT (protocol.h's own DW2_MAX_AUTH_TOKEN=900 comment), so a real token
     * always goes out as a separate AUTH message, matching apps/server/main.c's S_NEEDAUTH->AUTH
     * path. But dw2_server gives NO signal that auth is required after a token-less HELLO -- if
     * it doesn't (opt_noauth, the server's own default), it replies WELCOME immediately; if it
     * does, it silently sits in S_NEEDAUTH with no reply at all until AUTH arrives. Found live
     * (not assumed): blindly sending AUTH right behind HELLO whenever a token is in hand breaks a
     * no-auth server, which processes HELLO -> WELCOME first, then the now-unexpected AUTH second
     * -> DW2_ERR_BAD_STATE -> connection closed. Fix: probe with a short timeout first: a WELCOME
     * within it means no-auth already succeeded (skip AUTH entirely); no reply means the server is
     * silently waiting in S_NEEDAUTH (send AUTH now, then wait the real timeout for the outcome). */
    Dw2WireMsg reply; int got = 0;
    if (have_token) got = (recv_msg_blocking(&reply, 500, 1) == 1);
    if (have_token && !got) {
        memset(&m, 0, sizeof m); m.type = DW2_C_AUTH;
        size_t tl = strlen(token); if (tl > DW2_MAX_AUTH_TOKEN) tl = DW2_MAX_AUTH_TOKEN;
        m.u.auth.token_len = (uint16_t)tl; memcpy(m.u.auth.token, token, tl);
        send_msg(&m);
        got = (recv_msg_blocking(&reply, connect_timeout_ms, 0) == 1);
    } else if (!have_token) {
        got = (recv_msg_blocking(&reply, connect_timeout_ms, 0) == 1);
    }
    if (!got) { fprintf(stderr, "no response from server (timed out)\n"); return 1; }
    if (reply.type == DW2_S_WELCOME) {
        printf("WELCOME session=%u flags=%u\n", reply.u.welcome.session_id, reply.u.welcome.flags);
    } else if (reply.type == DW2_S_ERROR) {
        fprintf(stderr, "server rejected connection: error code %u%s\n", reply.u.error.code,
                have_token ? "" : " (does this server require --token/--iduna-url?)");
        return 1;
    } else { fprintf(stderr, "unexpected message type %d during handshake\n", reply.type); return 1; }

    memset(&m, 0, sizeof m); m.type = DW2_C_QUEUE; send_msg(&m);
    state = CS_QUEUED;
    dw2_nonblock(sock);

    if (selftest_mode) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("DEADWEIGHT_2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    printf("DEADWEIGHT_2 -- connected as %s, queued.\n", name);
    printf("  packing: 1-7 select | arrows/WASD cursor | R rotate | ENTER place | C panic cut | F ready\n");
    printf("  combat:  0-9 panic cut by placement index | ESC quit\n");

    Uint32 start_ms = SDL_GetTicks(); int exit_code = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && !selftest_mode) handle_key(e.key.keysym.sym);
            else if (e.type == SDL_KEYDOWN && selftest_mode && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }
        poll_incoming();
        if (state == CS_ERROR) { exit_code = 1; if (selftest_mode) running = 0; }
        if (state == CS_DONE && selftest_mode) running = 0;
        if (selftest_mode && SDL_GetTicks() - start_ms > (Uint32)selftest_timeout_ms) {
            fprintf(stderr, "selftest: timed out in state %d\n", state); exit_code = 1; running = 0;
        }
        render();
        SDL_Delay(16);
    }

    dw2_close(sock);
    SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit();
    return exit_code;
}
