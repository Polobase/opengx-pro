/* nx UI components (docs/nx/design/ui.md §2.10, §4.3, §5.6): title and status bar pieces, the
 * scrolling list with its sliding highlight and marquee, form rows (value -/+, toggles, links,
 * bars), the bottom sheet's frame, cards and hold rings. Every draw function is pure (gc +
 * arguments); the list's animation state lives in the screen's state block and is advanced by
 * nx_list_tick. */
#pragma once

#include <stdint.h>

#include "nx_ui_int.h"

/* ---- bars ---- */
/* Page title bar 0..21: the title from the left margin (after a "‹" when back), cut with "…"
 * before x1. */
void nx_comp_title(nx_gc *gc, const char *title, int16_t x1, int back);
void nx_comp_hints(nx_gc *gc, uint16_t sid);   /* key-hint bar NX_HINT_Y..239 */
/* The title bar with "<label> (G1) G2" at its right margin, the target generator filled, the
 * other outlined; the title ends before the label. */
void nx_comp_title_target(nx_gc *gc, const char *title, uint16_t label, unsigned target);
/* "G1"/"G2" pill in the generator's accent (filled) or outlined; returns its right edge. */
int16_t nx_comp_badge(nx_gc *gc, int16_t x, int16_t y, unsigned ch, int filled);
uint8_t nx_accent(unsigned ch);                               /* NX_PAL_G1 / NX_PAL_G2 */

/* ---- list (rows of row_h px, `rows` visible, from y0) ---- */
enum { NX_LA_NONE = 0, NX_LA_SLIDE, NX_LA_SCROLL };
#define NX_ROW_H 32
#define NX_SLIDE_MS 120u    /* highlight slide (ui.md §4.5) */
#define NX_SCROLL_MS 130u   /* viewport scroll, about 4 frames */

struct nx_list {
    int16_t y0;           /* viewport top */
    uint8_t row_h, rows;  /* row height, rows in the viewport */
    uint8_t n, sel, top;  /* rows in the list, selected row, first visible row */
    uint8_t anim, from;   /* NX_LA_*; the highlight's old row (SLIDE) / the old top (SCROLL) */
    uint8_t filled;       /* 1: the original's program list: the rows in a framed box 2 px around
                           * the viewport, the highlight a bar in the accent colour; rows get sel = 1
                           * while the bar covers them (nx_list_init: 0) */
    uint32_t t0;          /* start of the animation, and of the selected row's marquee */
};

typedef void (*nx_row_fn)(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms);

void nx_list_init(struct nx_list *l, int16_t y0, uint8_t row_h, uint8_t rows, uint8_t n, uint8_t sel, uint32_t now);
/* UP/DOWN (PRESS and REPEAT) move the selection: 1 = moved. Repeats and reduced motion jump. */
int nx_list_key(struct nx_list *l, const struct nx_kev *e, uint32_t now);
void nx_list_select(struct nx_list *l, uint8_t sel, uint32_t now);   /* jump, no animation */
void nx_list_tick(struct nx_list *l, uint32_t now);                  /* invalidates animation frames */
nx_rect nx_list_view(const struct nx_list *l);
nx_rect nx_list_row(const struct nx_list *l, unsigned i);            /* at rest (may be off view) */
/* Rows, highlight (surface2 + 3 px accent bar, or the framed box and a bar in the accent colour
 * when filled) and scrollbar for the frame at nx_ui_now(). */
void nx_list_draw(nx_gc *gc, const struct nx_list *l, uint8_t accent, nx_row_fn fn, const void *ctx);

/* Marquee of a focused text: the offset for sel_ms (0 without motion or when it fits). */
int16_t nx_marquee_at(int16_t text_w, int16_t box_w, uint32_t sel_ms);
/* In tick: invalidates box when the marquee position changed since *last. */
void nx_marquee_tick(int16_t *last, int16_t text_w, int16_t box_w, uint32_t sel_ms, nx_rect box);
/* A text in box: marquee when focused and too long, else cut with "…". */
void nx_text_box(nx_gc *gc, const nx_font_t *f, nx_rect box, const char *s, uint16_t c, int focused,
                 uint32_t sel_ms);

/* ---- form rows (settings, quick settings): label left, value right ---- */
enum { NX_ROW_INFO = 0, NX_ROW_LINK, NX_ROW_VALUE, NX_ROW_TOGGLE, NX_ROW_ACTION };
/* kind NX_ROW_TOGGLE draws a switch for on; NX_ROW_VALUE shows "‹ value ›" when selected. */
void nx_form_row(nx_gc *gc, nx_rect r, const char *label, const char *value, uint8_t kind, int on, int sel);
void nx_form_bar(nx_gc *gc, nx_rect r, const char *label, uint16_t frac, const char *value, int sel);

/* ---- sheets, cards, rings ---- */
void nx_sheet_frame(nx_gc *gc, int16_t y0);   /* bottom sheet from y0: rounded top, handle */
/* A menu cell: icon over its label (cut with "…"), the selected one on surface2 with a focus frame. */
void nx_grid_cell(nx_gc *gc, nx_rect r, uint8_t icon, const char *label, int sel);
void nx_card(nx_gc *gc, nx_rect r);
/* Ring of radius r (width w) around (cx, cy) filled to frac (0..65535) in token tok. */
void nx_hold_ring(nx_gc *gc, int16_t cx, int16_t cy, uint8_t r, uint8_t w, uint16_t frac, uint8_t tok);
/* Text centred horizontally on cx. */
void nx_text_c(nx_gc *gc, const nx_font_t *f, int16_t cx, int16_t y, const char *s, uint16_t c);
/* Text right-aligned to x1; returns its left edge. */
int16_t nx_text_r(nx_gc *gc, const nx_font_t *f, int16_t x1, int16_t y, const char *s, uint16_t c);
