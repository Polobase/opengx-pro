/* Host model of the ST7789 behind nx_lcd_port.h: GRAM in physical order (240 columns x 320 rows)
 * with the MADCTL mapping, window auto-increment, VSCRDEF/VSCSAD scanout, sleep and display off
 * of tools/sim/devices.py (St7789), so host renders and emulator screenshots agree pixel for pixel.
 * nxp_glass() is what the glass shows in logical (landscape) order. Transfers complete at once,
 * or with nxp_async = 1 only when the test calls nxp_complete() (the DMA interrupt). */
#pragma once

#include <stdint.h>

#define NXP_LOG_MAX 512u

extern uint16_t nxp_gram[320][240];
extern uint8_t nxp_madctl, nxp_sleep, nxp_disp_on, nxp_scrolling;
extern uint16_t nxp_vscrdef[3], nxp_vsp;
extern uint32_t nxp_pixels;          /* pixels written */
extern uint8_t nxp_log[NXP_LOG_MAX]; /* commands in order (RAMWR included) */
extern unsigned nxp_nlog;
extern unsigned nxp_bad;             /* protocol errors: pixels without RAMWR, bus use during a transfer */
extern int nxp_async;

void nxp_reset(void);                /* GRAM black, as after lcd_init: awake, display on, MADCTL 0 */
int nxp_complete(void);              /* async: end the transfer in flight (calls nx_lcd_irq); 0: none */
uint16_t nxp_at(int x, int y);       /* glass pixel at logical (x, y); 0 while dark */
void nxp_glass(uint16_t *out);       /* 320 x 240 glass pixels, row-major */
