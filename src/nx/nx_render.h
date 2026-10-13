/* nx render scheduler (docs/nx/design/ui.md §2.4, §2.5, §2.14, §2.15): dirty rectangles, drawn
 * top to bottom in bands of at most NX_BAND_PX pixels (h = min(rect_h, NX_BAND_PX / rect_w)) into
 * the two band buffers while DMA sends the previous band. Every band is composed from the
 * background and the layers in z-order (screen, [scrim], sheet, toast, overlay), each a draw(gc)
 * callback that paints its content from the state it took at the frame start (the frame hook),
 * so a frame never mixes two states.
 *
 * A band (or a whole dirty rectangle) that no layer primitive touches is sent as a solid fill of
 * the background colour: no drawing, no buffer. nx_render_step works for at most budget_us of CPU
 * and returns; the DMA interrupt keeps the bus busy meanwhile. */
#pragma once

#include <stdint.h>

#include "nx_gfx.h"

enum nx_layer {
    NX_LAYER_SCREEN = 0,   /* the screen; drawn with the scrim tint while a modal is open */
    NX_LAYER_SHEET,        /* bottom sheets, dialogs */
    NX_LAYER_TOAST,
    NX_LAYER_OVERLAY,      /* key lock, power ring */
    NX_LAYER_COUNT,
};

#define NX_DIRTY_MAX 16u
#define NX_SCRIM_LEVEL 18u   /* 55 % towards bg (docs/nx/design/ui.md §2.4) */

typedef void (*nx_draw_fn)(nx_gc *gc, void *ctx);

void nx_render_init(void);   /* dark theme, solid bg, no layers, the whole screen dirty */
void nx_render_theme(uint8_t theme);   /* NX_THEME_*: palette switch, everything dirty */
uint8_t nx_render_theme_get(void);
const uint16_t *nx_render_pal(void);
#if NX_ENG_VGRAD
void nx_render_bg(uint8_t tok_top, uint8_t tok_bottom);   /* equal: solid (fill fast path) */
#endif
void nx_render_scrim(uint8_t level32);                     /* 0 = off; tints bg and screen layer */
void nx_render_layer(uint8_t layer, nx_draw_fn fn, void *ctx);   /* fn NULL: layer off */
void nx_render_layer_clip(uint8_t layer, const nx_rect *clip);  /* NULL: the whole screen */
void nx_render_layer_offset(uint8_t layer, int16_t dx, int16_t dy);
#if NX_ENG_FADE
void nx_render_layer_fade(uint8_t layer, uint8_t level32);      /* 32 = opaque, 0 = bg colour */
#endif
/* Called when a frame starts (before its first band): the UI copies its model for drawing. */
void nx_render_on_frame(void (*fn)(void));

void nx_inval(nx_rect r);
void nx_inval_all(void);
/* Invalidates the tabular cells (nx_text_cells at (x, y), font f) whose byte differs between old
 * and new; everything from the first cell on when the lengths differ. */
void nx_inval_text(const nx_font_t *f, int16_t x, int16_t y, const char *old, const char *now);

int nx_render_step(uint32_t budget_us);   /* 1 while work remains */
int nx_render_busy(void);                 /* work that can progress (panel awake) */
int nx_render_flush(void);                /* render everything now; bounded; 0 on timeout */

/* Pure helpers, host-tested. */
int16_t nx_band_rows(int16_t w, int16_t h);
int nx_rect_mergeable(const nx_rect *a, const nx_rect *b);
unsigned nx_dirty_count(void);
nx_rect nx_dirty_get(unsigned i);

/* For the transitions (nx_trans.c): drop the pending rectangles and start a frame (frame hook);
 * compose and queue one band now (0: no buffer or queue room yet); work in flight. */
void nx_render_restart(void);
int nx_render_band(const nx_rect *band);
int nx_render_pending(void);

struct nx_render_stats {
    uint32_t frames, bands, fills;
    uint32_t draw_us;        /* CPU time composing bands, in total */
    uint16_t band_us_max;    /* longest composition of one band */
    uint16_t step_us_max;    /* longest nx_render_step */
    uint32_t frame_ms_last;  /* first band to the last pixel of the last frame */
};
extern struct nx_render_stats g_nx_render_stats;
