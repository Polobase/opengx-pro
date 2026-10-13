/* Hero frequency (docs/nx/design/ui.md §2.8): a generator frequency v = m * 10 + e (Hz =
 * m * 10^(e - 8), the w24 encoding of state.h) as up to 8 integer and 8 decimal digits in one
 * 296 px line. The big part (integer, grouped by 3 with a 7 px gap, the point, k decimals) is set
 * in D1 (F_NUM48) or D2 (F_NUM28); the remaining 8 - k decimals follow as a small "tail" in B1
 * (F_BODY16) top-aligned with the big digits, "Hz" below it on the big digits' baseline. Trailing
 * zeros of the tail are "ghost zeros" (text3). The layout is the first of D1 k = 3..0, then
 * D2 k = 8..0 that fits: pure arithmetic on the fonts' advances, host-tested.
 * Without NX_FEAT_HERO48 (nx_config.h; the image's default) there is no D1 and the 48 px font is
 * not linked: the layout is the first of D2 k = 3..0 that fits, the tail and "Hz" on its
 * baseline. */
#pragma once

#include <stdint.h>

#include "nx_config.h"
#include "nx_gfx.h"

#define NX_HERO_W 296
#define NX_HERO_GAP_D1 7   /* group gap in the D1 digits */
#define NX_HERO_GAP_D2 4
#define NX_HERO_GAP_TAIL 3
#define NX_HERO_TAIL_DX 6  /* big part to tail */

enum { NX_HERO_D1 = 0, NX_HERO_D2 = 1 };
#define NX_HERO_FIRST (NX_FEAT_HERO48 ? NX_HERO_D1 : NX_HERO_D2)   /* the first font the layout tries */
#define NX_HERO_IS_D1(font) (NX_FEAT_HERO48 && (font) == NX_HERO_D1)

typedef struct {
    uint32_t ip;          /* integer Hz (0 .. 99999999) */
    uint32_t frac;        /* 8 decimals: frac / 10^8 Hz */
    uint8_t over;         /* v was 10^8 Hz or more: clamped to 99999999.99999999 */
    uint8_t font;         /* NX_HERO_D1 / NX_HERO_D2 */
    uint8_t k;            /* decimals in the big font */
    uint8_t n_big, n_tail;
    char big[20];         /* integer digits, '.', k decimals (no spaces) */
    char tail[12];        /* '.' when k == 0, then the 8 - k decimals */
    uint32_t big_gaps;    /* bit i: group gap before big[i] */
    uint32_t tail_gaps;   /* bit i: group gap before tail[i] */
    uint8_t ghost;        /* tail[ghost..] are ghost zeros */
    int16_t w_big, w_tail, w_unit, w;
} nx_hero;

/* Split v into integer Hz and 8 decimals (clamped, see over). */
void nx_hero_decode(uint64_t v, uint32_t *ip, uint32_t *frac, uint8_t *over);
/* Choose the layout for v; returns 1 when one fits NX_HERO_W (always, down to D2 k = 0). */
int nx_hero_layout(uint64_t v, nx_hero *h);
/* Width of v in one candidate layout (font, k), as nx_hero_layout measures it. */
int16_t nx_hero_width(uint64_t v, uint8_t font, uint8_t k);
/* Draw with the line top at y: big digits c_text, tail c_text / c_ghost, "Hz" c_unit. */
void nx_hero_draw(nx_gc *gc, const nx_hero *h, int16_t x, int16_t y, uint16_t c_text, uint16_t c_ghost,
                  uint16_t c_unit);
/* Height of the hero line for a font choice (the big font's line height). */
int16_t nx_hero_height(const nx_hero *h);
/* The font of the big digits of a layout. */
const nx_font_t *nx_hero_font(const nx_hero *h);
