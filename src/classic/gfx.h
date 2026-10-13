/* Drawing primitives of the original UI (docs/spec/display.md §6-7), same semantics and quirks.
 *
 * All coordinates are PHYSICAL (x = column 0..239, y = row 0..319, MADCTL = 0), exactly the
 * arguments the original passes; the UI is seen rotated (logical X = 319 - y, Y = x). All blits
 * are opaque. Coordinates are 16-bit and wrap like the original's `& 0xFFFF`. Colour arguments
 * keep the original order: (bg, fg) for text and labels, (c0, c1) = colour of 0-bits / 1-bits
 * for the two icons whose symbol is made of 0-bits. */
#pragma once

#include <stdint.h>

void gfx_pixel(uint16_t x, uint16_t y, uint16_t c);                    /* 0x08008688 */
void gfx_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t c);      /* 0x08008FBC: x..x+len-1 */
void gfx_vline(uint16_t x, uint16_t y0, uint16_t y1, uint16_t c);      /* 0x08008F8E: y0..y1 */
/* 0x08009240: spans cx-r .. cx+r-1, rows cy-r .. cy+r (the original's integer algorithm) */
void gfx_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t c);
/* 0x08007A00: block x..x+w-1 x y..y+h (h+1 rows), r=8 corners on side 0 (low x) or 1 (high x),
 * corner pixels written black */
void gfx_fill_half_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c, uint8_t side);
/* 0x080086A4: x..x+w-1 x y..y+h, 2 px border, r=8 corners on all sides painted bg,
 * including the original's one-pixel quirk at (x+2, y+h-7) */
void gfx_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg, uint16_t border,
                         uint16_t fill);

/* font_ui24 record {code, width, 48 B} of `code`, or 0 (linear search like the original). */
const uint8_t *gfx_glyph_ui24(uint8_t code);
/* 0x0800616C: glyph at x..x+23, y-w+1..y; returns w (0 = unknown code, nothing drawn) */
uint16_t gfx_char_ui24(uint8_t ch, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);
void gfx_string_ui24(const char *s, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);   /* 0x08006358 */
/* 0x08006384: 5 digits with leading zeros; returns the y after the last digit */
uint16_t gfx_uint5_ui24(uint16_t v, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);
uint16_t gfx_char_freq24(uint8_t ch, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);  /* 0x08005DA4 */
/* 0x08005BDC: v = mantissa*10 + exponent digit, shown as "IIIIIIII.FFFFFFFF Hz" (value/1e8 Hz) with
 * the original's leading-zero layout */
void gfx_frequency(uint64_t v, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);

void gfx_gnum_label(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);   /* 0x08005AE4 '1'/'2' */
void gfx_out_label(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);    /* 0x08005E44 '1'/'2' */
void gfx_status_icon(uint8_t code, uint16_t x, uint16_t y, uint16_t bg, uint16_t fg);  /* 0x08006070 'c','r','p','s' */
void gfx_usb_icon(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1);                   /* 0x08006104 */
void gfx_sync_icon(uint16_t x, uint16_t y, uint16_t c0, uint16_t c1);                  /* 0x08005B74 */

/* 0x08006450: 24 x 230 px text field at x..x+23, y-229..y from a rendered name bitmap
 * (3 bytes per column, column 0 = logical right end of the text, `width` columns, `size` bytes
 * valid, zero beyond `width`), static or scrolled by `pos` (the marquee offset). */
void gfx_marquee_draw(uint16_t x, uint16_t y, uint16_t bg, uint16_t fg, const uint8_t *bmp, uint16_t size,
                      uint16_t width, uint16_t pos);
