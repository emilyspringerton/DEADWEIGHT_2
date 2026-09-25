/* dw2_palette -- D2's real color palette, ported verbatim from DEADWEIGHT's own
 * apps/gui/main.c Col constants (founder real-time, 2026-09-25: "use the art direction from
 * DEADWEIGHT 1 ... for the D2 game"). `DEADWEIGHT/docs/BRAND_STYLE_GUIDE.md` §2A names this
 * exact palette as the brand's real, current, going-forward visual identity (brutalist terminal:
 * flat fills, hard 2px frames, zero gradients, all-caps monospace) -- not invented for this port.
 *
 * The Offense/Operations/Defense kind triangle below is a real, exact, independent match to what
 * apps/local/main.c and apps/client/main.c already hard-coded for D2's own Dw2Kind colors
 * (COL_KIND[3] = {215,70,60},{225,160,40},{70,140,230}) before this file existed -- the two
 * projects already converged on the identical hex values without coordination, since both
 * inherited the same real design lineage (DEADWEIGHT/NORTHSTAR.md). This file makes that
 * inheritance explicit and gives every other D2 UI surface (backgrounds, text, frames) the same
 * real brand colors DEADWEIGHT's own desktop client already ships, not just the kind triangle. */
#ifndef DW2_PALETTE_H
#define DW2_PALETTE_H
#include <stdint.h>

typedef struct { uint8_t r, g, b; } Dw2Col;

#define DW2_COL(name, r, g, b) static const Dw2Col name = { (uint8_t)(r), (uint8_t)(g), (uint8_t)(b) }

DW2_COL(DW2_COL_BG,          18,  20,  28);   /* Terminal Black -- screen background */
DW2_COL(DW2_COL_PANEL,       32,  36,  50);   /* Corporate Grey -- panel/box fill */
DW2_COL(DW2_COL_TEXT,       235, 235, 240);   /* Signal White -- primary text */
DW2_COL(DW2_COL_DIM,        130, 135, 150);   /* Dim Grey -- secondary/dim text, labels */
DW2_COL(DW2_COL_GOOD,        80, 200, 120);   /* Clearance Green -- success, good outcomes */
DW2_COL(DW2_COL_BAD,        225,  80,  70);   /* Error Red -- failure, defeat */
DW2_COL(DW2_COL_SEL,        255, 255, 255);   /* Cursor White -- active focus/selection frame */
DW2_COL(DW2_COL_LOCK,        90,  90, 105);   /* Locked Grey -- inactive/locked frame */
DW2_COL(DW2_COL_GOLD,       255, 200,  60);   /* Access Gold -- premium/paid accents */

/* Offense/Operations/Defense -- matches Dw2Kind's own DW2_KIND_OFFENSE/OPERATIONS/DEFENSE order. */
static const Dw2Col DW2_COL_KIND[3] = { { 215, 70, 60 }, { 225, 160, 40 }, { 70, 140, 230 } };

#undef DW2_COL
#endif
