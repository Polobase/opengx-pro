/* nx raster core (docs/nx/design/ui.md §2.4, §2.6, §2.7): immediate-mode primitives that draw into
 * one band buffer at a time, clipped, anti-aliased where it matters, coloured from the theme's
 * palette tokens (nx_assets.h NX_PAL_*).
 *
 * A primitive first intersects its bounding box with the clip (a few cycles when it misses the
 * band). Every primitive that reaches the clip counts in gc->hits; with gc->px == NULL (probe)
 * nothing is drawn, so the renderer can learn that a band holds only the background and send a
 * solid fill instead of pixels.
 *
 * Anti-aliasing is exact without reading the panel back: all layers are drawn into the same band
 * buffer in z-order, and every edge blends over what is already there. Alpha is 0..32 (32 =
 * opaque) on the spread RGB565 form; the 0..15 alpha of the glyph tables (nx_asset_fmt.h) maps to
 * it through k_nx_a16. Sub-pixel geometry uses Q4 (1/16 px) coordinates. Colours taken with
 * nx_col() follow the layer's scrim and fade; raw RGB565 values are drawn as they are. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nx_assets.h"
#include "nx_lcd.h"

typedef struct nx_gc {
    uint16_t *px;          /* band buffer, row-major, (band.x1 - band.x0) pixels per row; NULL: probe */
    nx_rect band;          /* screen area the buffer holds */
    nx_rect clip;          /* drawing area, always inside band */
    const uint16_t *pal;   /* palette of the theme (NX_PAL_COUNT tokens) */
    int16_t ox, oy;        /* added to every coordinate (layer offset: toast slide) */
    uint8_t tint;          /* 0..32: token colours blended this far towards pal[NX_PAL_BG] (scrim, fade) */
    uint16_t hits;         /* primitives that reached the clip */
} nx_gc;

extern const uint8_t k_nx_a16[16];   /* glyph alpha 0..15 -> 0..32 */

/* ---- geometry helpers ---- */
static inline nx_rect nx_rect_xywh(int16_t x, int16_t y, int16_t w, int16_t h)
{
    nx_rect r = { x, y, (int16_t)(x + w), (int16_t)(y + h) };
    return r;
}
int nx_rect_empty(const nx_rect *r);
nx_rect nx_rect_and(nx_rect a, nx_rect b);    /* intersection (empty: x0 >= x1 or y0 >= y1) */
nx_rect nx_rect_or(nx_rect a, nx_rect b);     /* bounding box of both (an empty one is ignored) */
int32_t nx_rect_area(const nx_rect *r);

/* ---- colour ---- */
uint16_t nx_mix(uint16_t bg, uint16_t fg, unsigned a32);   /* bg + (fg - bg) * a / 32 per channel */
uint16_t nx_col(const nx_gc *gc, uint8_t tok);             /* palette token with the gc's tint */
/* Narrows the clip to r (screen coordinates, the gc offset applied); returns the previous clip. */
nx_rect nx_clip_push(nx_gc *gc, nx_rect r);
void nx_clip_pop(nx_gc *gc, nx_rect prev);

/* ---- primitives (coordinates before the gc offset; rectangles half-open) ---- */
void nx_fill(nx_gc *gc, nx_rect r, uint16_t c);
#if NX_VENDOR_SPLASH
/* 1-bit mask of r's size (rows MSB first, (w + 7) / 8 bytes each): set bits in c, the rest left. */
void nx_mask1(nx_gc *gc, nx_rect r, const uint8_t *bits, uint16_t c);
#endif
#if NX_ENG_VGRAD
/* Vertical gradient top -> bottom with 4x4 Bayer dither (no banding in dark ramps). */
void nx_vgrad(nx_gc *gc, nx_rect r, uint16_t top, uint16_t bottom);
#endif
/* Rounded rectangle, filled / outline of width w; rad is clamped to half the shorter side. */
void nx_rrect(nx_gc *gc, nx_rect r, uint8_t rad, uint16_t c);
void nx_rrect_line(nx_gc *gc, nx_rect r, uint8_t rad, uint8_t w, uint16_t c);
void nx_pill(nx_gc *gc, nx_rect r, uint16_t c);   /* rad = h / 2 */
/* Progress bar: track pill, fill pill up to frac (0..65535 of the width) with a sub-pixel end. */
void nx_bar(nx_gc *gc, nx_rect r, uint16_t frac, uint16_t fill, uint16_t track);
/* Segment bar (chains, presets): widths proportional to dur[] (min 4 px, 2 px gaps), segments
 * before cur in c_done, cur filled to cur_frac (0..65535) over c_todo, the rest c_todo. */
#define NX_SEG_MAX 16u
#define NX_SEG_MIN_W 4
#define NX_SEG_GAP 2
void nx_segbar(nx_gc *gc, nx_rect r, const uint16_t *dur, uint8_t n, uint8_t cur, uint16_t cur_frac,
               uint16_t c_done, uint16_t c_cur, uint16_t c_todo);
/* Pure layout of nx_segbar: segment i spans [x0[i], x1[i]) relative to the bar's left edge. */
void nx_segbar_layout(int16_t w, const uint16_t *dur, uint8_t n, int16_t *x0, int16_t *x1);
/* Anti-aliased ring (annulus of outer radius r and width w around the pixel corner (cx, cy)) from
 * angle a0 to a1 clockwise, angles in 1/1024 turn from 12 o'clock (a1 - a0 >= 1024: full ring);
 * caps: round ends. */
void nx_ring(nx_gc *gc, int16_t cx, int16_t cy, uint8_t r, uint8_t w, uint16_t a0, uint16_t a1, int caps,
             uint16_t c);
/* Anti-aliased disc, centre and radius in Q4. */
void nx_disc(nx_gc *gc, int32_t cx_q4, int32_t cy_q4, int32_t r_q4, uint16_t c);
/* sin / cos of an angle in 1/1024 turn, Q14 (64-entry quarter-sine table, interpolated). */
int32_t nx_sin1024(uint32_t a);
int32_t nx_cos1024(uint32_t a);
/* Column-span polyline for the scope and sparklines: n columns from x0; y(ctx, i) gives the curve
 * in Q4 screen coordinates for column i (called for i = -1 .. n); half_w_q4 is half the line
 * width; a32 its opacity. */
typedef int32_t (*nx_trace_fn)(void *ctx, int16_t i);
void nx_trace(nx_gc *gc, int16_t x0, int16_t n, nx_trace_fn y, void *ctx, int16_t half_w_q4, uint16_t c,
              unsigned a32);
/* Icon of an icon set (nx_icons20/32/brand) tinted with c, top-left at (x, y). */
void nx_icon(nx_gc *gc, const nx_font_t *set, uint8_t id, int16_t x, int16_t y, uint16_t c);

/* ---- text (fonts of nx_assets.h; (x, y) = pen position at the line top) ---- */
int16_t nx_text(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, const char *s, uint16_t c);
int16_t nx_text_n(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, const char *s, size_t n, uint16_t c);
/* s cut to max_w with the font's ellipsis (nx_font_fit); returns the end x. */
int16_t nx_text_fit(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, int16_t max_w, const char *s, uint16_t c);
/* s scrolled left by offset inside box (repeating after NX_MARQUEE_GAP px), clipped to box. */
void nx_text_marquee(nx_gc *gc, const nx_font_t *f, nx_rect box, const char *s, int16_t offset, uint16_t c);
/* Tabular cells: the left edge of every byte's cell and the end (cell_x[0..n]); returns n. */
unsigned nx_text_cells(const nx_font_t *f, const char *s, int16_t x, int16_t *cell_x, unsigned max);

#define NX_MARQUEE_GAP 30
/* Marquee position after t_ms (docs/nx/design/ui.md §2.7): 1 s still, then 2 px steps at 20 Hz
 * (40 px/s) through text_w + NX_MARQUEE_GAP, again and again; 0 when the text fits. */
int16_t nx_marquee_offset(int16_t text_w, int16_t box_w, uint32_t t_ms);
