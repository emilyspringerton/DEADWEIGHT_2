/* dw2_font -- D2's real text rendering, ported from DEADWEIGHT's apps/gui/main.c (founder
 * real-time, 2026-09-25: "use the art direction from DEADWEIGHT 1 ... for the D2 game"). The
 * 5x7 bitmap glyph table below is the same real, already-shipped Adafruit-GLCD-derived font
 * DEADWEIGHT's own desktop client uses -- `DEADWEIGHT/docs/BRAND_STYLE_GUIDE.md` names it
 * explicitly as this brand's real, current, going-forward visual identity ("brutalist... a
 * hand-rolled 5x7 bitmap font... not a placeholder aesthetic to be replaced later -- they're the
 * target"), not something invented for this port.
 *
 * Adapted, not copied verbatim: DEADWEIGHT's own text_a() reads an implicit static global
 * SDL_Renderer* and a Col typedef the includer must already have defined; this version takes the
 * renderer and raw RGB bytes as explicit parameters instead, so it has zero coupling to how
 * apps/local/main.c or apps/client/main.c name their own Col struct -- a real, small improvement
 * over the original convention, not a blind copy (same "ported and adapted" discipline
 * core/net.h/http.{h,c}/iduna.{h,c} already established for this repo). D2 had no text renderer
 * at all before this -- "the SDL window itself is deliberately bare-minimum (colored rectangles
 * only)" was a real, named gap in NORTHSTAR.md; this closes it. */
#ifndef DW2_FONT_H
#define DW2_FONT_H
#include <SDL.h>

/* Draws `s` at (x,y), `scale` px per glyph pixel, glyph cell 6*scale wide. Unknown chars fall
 * back to '?', matching DEADWEIGHT's own convention (and the same S522/S523 lesson already
 * learned there: keep the glyph table honest, don't let a missing char silently render as
 * something else without at least falling back visibly). */
void dw2_text(SDL_Renderer *r, int x, int y, int scale, uint8_t cr, uint8_t cg, uint8_t cb, const char *s);
/* Same, alpha-blended (0-255). */
void dw2_text_a(SDL_Renderer *r, int x, int y, int scale, uint8_t cr, uint8_t cg, uint8_t cb, int alpha, const char *s);
/* Centered on cx. */
void dw2_text_c(SDL_Renderer *r, int cx, int y, int scale, uint8_t cr, uint8_t cg, uint8_t cb, const char *s);
/* Pixel width of `s` at the given scale, for layout math. */
int dw2_text_w(int scale, const char *s);
#endif
