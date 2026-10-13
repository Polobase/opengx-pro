/* ST7789 240x320 panel on SPI2 (docs/spec/display.md).
 *
 * Physical coordinates are the controller's: x = column 0..239, y = row 0..319, MADCTL = 0.
 * The device is used in landscape; logical coordinates (X 0..319 left->right, Y 0..239
 * top->bottom) map as X = 319 - y, Y = x. All stored bitmaps and fonts are in physical order. */
#pragma once

#include <stdint.h>

#define LCD_W 240u   /* physical */
#define LCD_H 320u

/* RGB565 colours used by the original UI */
#define COLOR_BLACK     0x0000u
#define COLOR_WHITE     0xFFFFu
#define COLOR_BLUE      0x2B12u
#define COLOR_GREY      0x8430u
#define COLOR_RED       0xF800u
#define COLOR_YELLOW    0xFFE0u
#define COLOR_DARKBLUE  0x0016u

/* Reads the panel variant halfword at 0x08003C00 (written by the vendor, never by us),
 * resets and initialises the controller. Returns the halfword. */
uint16_t lcd_init(void);
char lcd_variant_name(uint16_t variant);   /* 'A' or 'B' */

/* Physical primitives, same semantics as the original. */
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
/* Opaque 1-bpp blit: window (x0..x1, y0..y1), `len` bytes MSB first, 1 = fg, 0 = bg. */
void lcd_blit1(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, const uint8_t *bits, uint32_t len,
               uint16_t fg, uint16_t bg);
/* Raw pixel stream into a window, for drawing code that computes its pixels (gfx.c):
 * lcd_stream_begin sets the window (x0..x1, y0..y1) and starts RAMWR, every lcd_stream_pixel
 * writes one RGB565 pixel (column first, then row, as the controller auto-increments), and
 * lcd_stream_end releases the bus. */
void lcd_stream_begin(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
void lcd_stream_pixel(uint16_t color);
void lcd_stream_end(void);
/* One font_ui24 glyph at physical (x, y) growing towards lower y; returns its width (0 = unknown code). */
uint16_t lcd_draw_char_ui24(char ch, uint16_t x, uint16_t y, uint16_t fg, uint16_t bg);

/* Logical (landscape) helpers */
/* Landscape (X,Y) -> physical (x,y) rectangle transform (X = 319 - y, Y = x, above), shared so
 * gfx_fill() and any other landscape-space caller (e.g. nx_ui_stub.c) fill through the same
 * definition: a future change to the panel's coordinate mapping (rotation/offset) then applies to
 * every caller instead of risking a private copy left behind. `static inline` keeps this out of
 * link-time dependencies, so callers that do not otherwise need lcd_draw.c (e.g. host tests, which
 * mock lcd_fill_rect() directly) still link. */
static inline void lcd_fill_rect_landscape(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1,
                                            uint16_t color)
{
    lcd_fill_rect(Y0, (uint16_t)(LCD_H - 1u - X1), Y1, (uint16_t)(LCD_H - 1u - X0), color);
}

void gfx_fill(uint16_t X0, uint16_t Y0, uint16_t X1, uint16_t Y1, uint16_t color);
/* Draws text with its top-left corner at (X, Y); returns the X after the last glyph. */
uint16_t gfx_text(uint16_t X, uint16_t Y, const char *s, uint16_t fg, uint16_t bg);
uint16_t gfx_text_width(const char *s);
