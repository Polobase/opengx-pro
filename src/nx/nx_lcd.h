/* nx panel driver (docs/nx/design/ui.md §2.2-2.3, §2.12): the ST7789 in landscape addressing
 * (NX_LCD_MADCTL), a queue of up to NX_LCD_QUEUE transfers sent in the background (band buffer,
 * solid fill, hardware scroll, panel command), the two band buffers, and the panel power states.
 *
 * Coordinates are logical: X 0..319 left to right, Y 0..239 top to bottom; rectangles are
 * half-open. Nothing here blocks except nx_lcd_sync(), which waits a bounded time for the queue
 * to drain. The transfers themselves go through nx_lcd_port.h (DMA or polled on the device, the
 * panel model on the host). */
#pragma once

#include <stdint.h>

#include "nx_config.h"

#define NX_LCD_W 320
#define NX_LCD_H 240
#define NX_LCD_QUEUE 4u

typedef struct {
    int16_t x0, y0, x1, y1;   /* x0 <= x < x1, y0 <= y < y1 */
} nx_rect;

/* Panel power states (docs/nx/design/ui.md §2.12). */
enum nx_lcd_power {
    NX_LCD_ON = 0,   /* awake, display on */
    NX_LCD_BLANK,    /* awake, display off: GRAM can be written in the dark */
    NX_LCD_SLEEP,    /* display off and sleep in: GRAM kept, lowest power */
};

#define NX_LCD_SLPOUT_MS 5u      /* ST7789: after SLPOUT, wait before the next command */
#define NX_LCD_SLEEP_GAP_MS 120u /* ST7789: SLPIN and SLPOUT at least this far apart */

/* After lcd_init() (unchanged variant A/B lists, panel awake and on): landscape MADCTL, 16-bit
 * frames (NX_LCD_DS16), full-height scroll area, scroll 0, DMA. */
void nx_lcd_init(void);

/* Band buffers (2 x NX_BAND_PX pixels, RGB565). nx_lcd_buf_get returns a free one or NULL (both
 * are queued or in flight); it stays owned by the caller until nx_lcd_band queued it (the driver
 * frees it once sent) or nx_lcd_buf_put gave it back. */
uint16_t *nx_lcd_buf_get(void);
void nx_lcd_buf_put(uint16_t *buf);

/* Queue a transfer; each returns 0 (nothing queued) when the queue is full.
 *   nx_lcd_band    the pixels of buf (row-major, r's width per row) into window r;
 *                  w * h <= NX_BAND_PX; in the 8-bit mode the buffer is byte-swapped in place
 *   nx_lcd_fill    window r with one colour, any size
 *   nx_lcd_scroll  hardware scroll: afterwards the glass shows GRAM column (X + s) mod 320 at X
 *   nx_lcd_cmd     a parameterless panel command (SLPIN, DISPON, ...) */
int nx_lcd_band(uint16_t *buf, const nx_rect *r);
int nx_lcd_fill(const nx_rect *r, uint16_t color);
int nx_lcd_scroll(uint16_t s);
int nx_lcd_cmd(uint8_t cmd);
unsigned nx_lcd_room(void);   /* free queue entries */
int nx_lcd_idle(void);        /* nothing queued and nothing in flight */
uint16_t nx_lcd_scroll_pos(void);   /* the s of the last nx_lcd_scroll */

/* Polled mode: sends the next chunk (at most NX_BAND_PX pixels). DMA mode: nothing to do. The
 * render slice calls it. */
void nx_lcd_poll(void);
/* Bounded wait until the queue has drained (boot, calibration screen, goodbye). 0 on timeout. */
int nx_lcd_sync(void);
/* A transfer has finished: called by the DMA interrupt (nx_lcd_port.c) or the host panel model. */
void nx_lcd_irq(void);

/* Panel power: nx_lcd_power sets the target, nx_lcd_power_step (every render slice) sends the
 * commands when the queue is idle and the ST7789's minimum delays have passed. nx_lcd_ready: the
 * panel takes pixels now (awake, NX_LCD_SLPOUT_MS after SLPOUT). */
void nx_lcd_power(uint8_t target);
void nx_lcd_power_step(uint32_t now_ms);
uint8_t nx_lcd_power_state(void);
int nx_lcd_ready(void);

/* Diagnostics: transfers and pixels sent since nx_lcd_init. */
struct nx_lcd_stats {
    uint32_t bands, fills, scrolls, cmds;
    uint32_t pixels;
};
extern struct nx_lcd_stats g_nx_lcd_stats;

/* The ST7789 commands the driver sends (datasheet §9.1). */
#define NX_ST_SLPIN 0x10u
#define NX_ST_SLPOUT 0x11u
#define NX_ST_NORON 0x13u
#define NX_ST_DISPOFF 0x28u
#define NX_ST_DISPON 0x29u
#define NX_ST_CASET 0x2Au
#define NX_ST_RASET 0x2Bu
#define NX_ST_RAMWR 0x2Cu
#define NX_ST_VSCRDEF 0x33u
#define NX_ST_MADCTL 0x36u
#define NX_ST_VSCSAD 0x37u
