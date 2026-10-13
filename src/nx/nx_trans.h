/* nx transitions (docs/nx/design/ui.md §2.11): one at a time, driven by time from nx_render_step.
 *
 *   push    hardware-scroll push (G1 <-> G2, pages): NX_PUSH_STEPS steps of NX_PUSH_DX px over
 *           NX_PUSH_MS (ease-in-out step timing). Step k writes the new screen's columns
 *           [(k-1)dx, k dx) (push left) into the same GRAM columns, which show the old screen's
 *           leaving edge, then scrolls by dx: the new screen enters from the right, and after
 *           320 px the scroll is back at 0 with GRAM in identity. Push right is the mirror image.
 *           Normal rendering waits; invalidations meanwhile are drawn afterwards.
 *   dip     backlight to half the level (25 % light) in 60 ms, full render, back up in 120 ms:
 *           hides the repaint wipe of hierarchy changes; the reduced-motion push and reveal.
 *   reveal  bottom sheet: the scrimmed base is rendered once with the layer hidden, then the
 *           layer's rows are revealed from the bottom edge upwards over NX_REVEAL_MS.
 *   slide   a layer's offset moves from dy0 to dy1 (toast in/out), one frame at a time.
 *   fade    a layer fades in or out over NX_FADE_STEPS steps (colours towards bg).
 * Starting one while another runs returns 0 (the UI buffers its keys meanwhile). */
#pragma once

#include <stdint.h>

#include "nx_lcd.h"

#define NX_PUSH_MS 240u
#define NX_PUSH_DX 8
#define NX_PUSH_STEPS (NX_LCD_W / NX_PUSH_DX)
#define NX_DIP_DOWN_MS 60u
#define NX_DIP_UP_MS 120u
#define NX_REVEAL_MS 180u
#define NX_FADE_MS 300u
#define NX_FADE_STEPS 6u

enum { NX_PUSH_LEFT = 0, NX_PUSH_RIGHT };

int nx_trans_push(uint8_t dir);
int nx_trans_dip(void);
int nx_trans_reveal(uint8_t layer, nx_rect area);
int nx_trans_slide(uint8_t layer, nx_rect area, int16_t dy0, int16_t dy1, uint16_t ms);
#if NX_ENG_FADE
int nx_trans_fade(uint8_t layer, nx_rect area, int in);
#endif
int nx_trans_active(void);
/* Ends a running transition at once (re-init, tests): scroll back to 0, a dip's backlight
 * restored to the user level at once (unless the up-fade was already issued), layer clip and
 * offset restored, everything dirty. */
void nx_trans_cancel(void);
/* Called by nx_render_step: advances the transition; 1 = normal rendering waits this slice. */
int nx_trans_step(uint32_t now);
/* Push schedule, host-tested: GRAM columns of step k (1..NX_PUSH_STEPS) and the scroll after it. */
nx_rect nx_push_cols(uint8_t dir, unsigned k);
uint16_t nx_push_scroll(uint8_t dir, unsigned k);
