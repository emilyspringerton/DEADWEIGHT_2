/* DEADWEIGHT_2 local debug shell -- PHASE_D1_CORE_LOOP.md: "a single local binary, one human
 * player against a fixed, hand-authored dummy grid... bare-minimum debug render (immediate-mode
 * boxes)... not the real client UI; that's D3." No networking, no accounts, no art.
 *
 * Controls (printed to stdout on launch, since a bitmap font renderer is real UI-polish work this
 * phase explicitly scopes out):
 *   1-7        select an item from the catalog
 *   arrows/WASD move the grid cursor
 *   R          rotate the selected item / the item under the cursor
 *   ENTER      place the selected item at the cursor (packing phase only)
 *   C          Panic Cut the item under the cursor (legal in packing OR mid-combat)
 *   F          lock the grid and start the fight
 *   ESC        quit
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include "../../core/combat.h"
#include "../../core/dummy.h"
#include "../../core/items.h"

#define CELL_PX 64
#define GRID_ORIGIN_X 40
#define GRID_ORIGIN_Y 60
#define WIN_W 900
#define WIN_H 560
#define TICK_MS 700 /* 1 "tick" per TICK_MS real ms during combat -- matches core/combat.h's own
                       "1 tick = 1 second" comment loosely enough for a debug shell (not gameplay-
                       critical exactness; a real client can retune this in D3). */

typedef enum { MODE_PACK, MODE_COMBAT, MODE_DONE } AppMode;

typedef struct { Uint8 r, g, b; } Col;
static const Col COL_BG = { 18, 20, 28 }, COL_CURSOR = { 255, 255, 255 },
    COL_RUINED = { 90, 30, 30 }, COL_BLOCKADE = { 10, 10, 10 };
static const Col COL_KIND[3] = { { 215, 70, 60 }, { 225, 160, 40 }, { 70, 140, 230 } }; /* Offense/Ops/Defense */
static const Col COL_GENERATOR = { 80, 220, 120 }, COL_CONDUCTOR = { 200, 200, 210 },
    COL_SPLITTER = { 80, 210, 220 }, COL_WEAPON = { 240, 90, 40 }, COL_ARMOR = { 90, 140, 240 },
    COL_DEAD = { 70, 70, 70 };

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
            if (s->burned & dw2_bit(dw2_cell(pl->cells[c].row, pl->cells[c].col)))
                frame(x, y, CELL_PX - 2, CELL_PX - 2, (Col){ 255, 0, 0 }, 3);
        }
    }
    if (show_cursor) {
        int x = origin_x + cursor_col * CELL_PX, y = origin_y + cursor_row * CELL_PX;
        frame(x, y, CELL_PX - 2, CELL_PX - 2, COL_CURSOR, 3);
    }
    /* hull/armor bars beneath the grid */
    int bar_y = origin_y + DW2_GRID_H * CELL_PX + 12;
    int bar_w = DW2_GRID_W * CELL_PX - 2;
    float hull_frac = s->hull_pct / 100.0f; if (hull_frac < 0) hull_frac = 0;
    rect(origin_x, bar_y, bar_w, 14, (Col){ 40, 20, 20 });
    rect(origin_x, bar_y, (int)(bar_w * hull_frac), 14, (Col){ 220, 60, 60 });
    float armor_frac = s->armor / 100.0f; if (armor_frac > 1) armor_frac = 1;
    rect(origin_x, bar_y + 20, bar_w, 10, (Col){ 20, 30, 45 });
    rect(origin_x, bar_y + 20, (int)(bar_w * armor_frac), 10, COL_ARMOR);
}

static void find_placement_under_cursor(const Dw2Ship *s, int row, int col, int *out_idx) {
    *out_idx = -1;
    dw2_mask bit = dw2_bit(dw2_cell(row, col));
    for (int p = 0; p < s->placement_n; p++)
        if (s->placements[p].item_id >= 0 && (s->placements[p].mask & bit)) { *out_idx = p; return; }
}

/* Headless smoke test (matches DEADWEIGHT's own dw_gui --selftest convention): mirrors the dummy's
 * own loadout, locks in, and runs real ticks (through the real dw2_ship_tick/dw2_match_result
 * path, no shortcuts) until the match resolves or a safety cap is hit -- a real, running,
 * automated proof this binary builds and plays a full match end to end without a display. */
static int run_selftest(void) {
    Dw2Ship player; dw2_ship_init(&player);
    dw2_ship_place(&player, DW2_ITEM_GENERATOR, 0, 0, 0);
    dw2_ship_place(&player, DW2_ITEM_CONDUCTOR, 0, 1, 0);
    dw2_ship_place(&player, DW2_ITEM_RAILGUN, 0, 2, 0);
    dw2_ship_place(&player, DW2_ITEM_BULWARK, 3, 3, 0);
    dw2_ship_start_combat(&player);
    Dw2Ship dummy; dw2_build_dummy_ship(&dummy);

    Dw2MatchResult result = DW2_RESULT_ONGOING;
    int ticks = 0;
    for (; ticks < DW2_MATCH_TIMEOUT_TICKS + 5 && result == DW2_RESULT_ONGOING; ticks++) {
        dw2_ship_tick(&player, &dummy);
        dw2_ship_tick(&dummy, &player);
        result = dw2_match_result(&player, &dummy, ticks + 1);
        rect(0, 0, WIN_W, WIN_H, COL_BG);
        draw_ship(&player, GRID_ORIGIN_X, GRID_ORIGIN_Y, 0, 0, 0);
        draw_ship(&dummy, GRID_ORIGIN_X + DW2_GRID_W * CELL_PX + 60, GRID_ORIGIN_Y, 0, 0, 0);
        SDL_RenderPresent(R);
    }
    printf("selftest: result=%d hull=%.0f%%/%.0f%% ticks=%d\n", result, player.hull_pct, dummy.hull_pct, ticks);
    return result == DW2_RESULT_ONGOING ? 1 : 0;
}

int main(int argc, char **argv) {
    int selftest = 0;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--selftest")) selftest = 1;

    printf("DEADWEIGHT_2 -- local core-loop debug shell (Phase D1, no networking)\n");
    printf("  1-7 select item | arrows/WASD move cursor | R rotate | ENTER place | C panic cut | F fight | ESC quit\n");
    for (int i = 0; i < DW2_ITEM_COUNT; i++) printf("    [%d] %s\n", i + 1, dw2_catalog[i].name);

    if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("DEADWEIGHT_2 -- core loop debug shell", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    if (selftest) {
        int rc = run_selftest();
        SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit();
        return rc;
    }

    Dw2Ship player; dw2_ship_init(&player);
    Dw2Ship dummy; dw2_build_dummy_ship(&dummy);

    AppMode mode = MODE_PACK;
    int cursor_row = 0, cursor_col = 0, sel_item = 0, sel_rotation = 0;
    int elapsed_ticks = 0;
    Uint32 last_tick_ms = 0;
    Dw2MatchResult result = DW2_RESULT_ONGOING;

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                else if (mode == MODE_PACK && k >= SDLK_1 && k <= SDLK_7) {
                    sel_item = (int)(k - SDLK_1);
                    printf("selected: %s\n", dw2_catalog[sel_item].name);
                } else if (k == SDLK_UP || k == SDLK_w) cursor_row = cursor_row > 0 ? cursor_row - 1 : cursor_row;
                else if (k == SDLK_DOWN || k == SDLK_s) cursor_row = cursor_row < DW2_GRID_H - 1 ? cursor_row + 1 : cursor_row;
                else if (k == SDLK_LEFT || k == SDLK_a) cursor_col = cursor_col > 0 ? cursor_col - 1 : cursor_col;
                else if (k == SDLK_RIGHT || k == SDLK_d) cursor_col = cursor_col < DW2_GRID_W - 1 ? cursor_col + 1 : cursor_col;
                else if (k == SDLK_r) sel_rotation = (sel_rotation + 1) % 4;
                else if (k == SDLK_RETURN && mode == MODE_PACK) {
                    int idx = dw2_ship_place(&player, sel_item, cursor_row, cursor_col, sel_rotation);
                    printf(idx >= 0 ? "placed %s at (%d,%d)\n" : "illegal placement\n",
                           dw2_catalog[sel_item].name, cursor_row, cursor_col);
                } else if (k == SDLK_c) {
                    int idx; find_placement_under_cursor(&player, cursor_row, cursor_col, &idx);
                    if (idx >= 0) {
                        int ok = dw2_ship_panic_cut(&player, idx);
                        printf(ok ? "Panic Cut! %s fragmented.\n" : "Panic Cut failed (no room for fragments).\n",
                               dw2_catalog[player.placements[idx].item_id].name);
                    }
                } else if (k == SDLK_f && mode == MODE_PACK) {
                    dw2_ship_start_combat(&player);
                    mode = MODE_COMBAT;
                    last_tick_ms = SDL_GetTicks();
                    printf("Fight! hull 100%%/100%% vs dummy.\n");
                }
            }
        }

        if (mode == MODE_COMBAT) {
            Uint32 now = SDL_GetTicks();
            if (now - last_tick_ms >= TICK_MS) {
                last_tick_ms = now;
                dw2_ship_tick(&player, &dummy);
                dw2_ship_tick(&dummy, &player);
                elapsed_ticks++;
                result = dw2_match_result(&player, &dummy, elapsed_ticks);
                if (result != DW2_RESULT_ONGOING) {
                    mode = MODE_DONE;
                    const char *msg = result == DW2_RESULT_SELF_WIN ? "YOU WIN" :
                                       result == DW2_RESULT_ENEMY_WIN ? "YOU LOSE" : "TIE";
                    printf("%s -- hull %.0f%% vs %.0f%% (%d ticks)\n", msg, player.hull_pct, dummy.hull_pct, elapsed_ticks);
                } else {
                    printf("tick %d -- hull %.0f%% vs %.0f%%\n", elapsed_ticks, player.hull_pct, dummy.hull_pct);
                }
            }
        }

        rect(0, 0, WIN_W, WIN_H, COL_BG);
        draw_ship(&player, GRID_ORIGIN_X, GRID_ORIGIN_Y, cursor_row, cursor_col, mode != MODE_DONE);
        draw_ship(&dummy, GRID_ORIGIN_X + DW2_GRID_W * CELL_PX + 60, GRID_ORIGIN_Y, 0, 0, 0);
        SDL_RenderPresent(R);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
