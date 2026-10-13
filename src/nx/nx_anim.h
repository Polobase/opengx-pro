/* nx motion (docs/nx/design/ui.md §2.7, §4.5): time-based easing and tweens, the odometer roll of
 * tabular digits with its accent tint, and the reduced-motion switch. Everything is a function of
 * the time passed in (sampled once per frame): dropped frames skip ahead, never slow down.
 * Progress values are Q16 (0 .. 65535 = done). */
#pragma once

#include <stdint.h>

#include "nx_gfx.h"

enum nx_ease { NX_EASE_LINEAR = 0, NX_EASE_OUT, NX_EASE_IN, NX_EASE_IN_OUT };

/* Reduced motion (Settings): tweens end at once, rolls and scope phase stop, the push and the
 * sheet reveal become a backlight dip, slides and fades are instant. */
void nx_anim_set_reduced(int on);
int nx_anim_reduced(void);

uint16_t nx_ease(uint8_t ease, uint16_t p);   /* cubic curves on Q16 */
/* Progress of an animation started at t0 lasting dur ms, eased; 65535 when over (or reduced). */
uint16_t nx_anim_p(uint32_t t0, uint32_t now, uint16_t dur, uint8_t ease);

typedef struct {
    uint32_t t0;
    uint16_t dur;
    int16_t from, to;
    uint8_t ease;
} nx_tween;

void nx_tween_start(nx_tween *tw, int16_t from, int16_t to, uint16_t dur, uint8_t ease, uint32_t now);
int16_t nx_tween_at(const nx_tween *tw, uint32_t now);
int nx_tween_running(const nx_tween *tw, uint32_t now);

/* Odometer roll of a tabular number (hero frequency, step counter): a changed cell rolls over
 * NX_ROLL_MS (the old digit moves up, the new one comes in from below, clipped to the cell),
 * cells start NX_ROLL_STAGGER_MS apart from right to left, and each tints to the accent and
 * fades back over NX_TINT_MS. More than NX_ROLL_LIVE_N updates within a second (sweeps, PC scans):
 * digits swap instantly until a second passes without one. */
#define NX_ROLL_MAX 24
#define NX_ROLL_MS 160u
#define NX_ROLL_STAGGER_MS 20u
#define NX_TINT_MS 400u
#define NX_ROLL_LIVE_N 3u

typedef struct {
    char from[NX_ROLL_MAX + 1];   /* text before the change */
    char to[NX_ROLL_MAX + 1];     /* text now */
    uint8_t delay[NX_ROLL_MAX];   /* start of each changed cell after t0, in NX_ROLL_STAGGER_MS; 0xFF = unchanged */
    uint32_t t0;
    uint32_t win_t0;              /* live rule: start of the current 1 s window */
    uint8_t win_n;                /* updates in the window */
    uint8_t live;                 /* 1: instant swaps */
    uint8_t rolling;              /* 1 while any cell may still move or tint */
} nx_roll;

void nx_roll_init(nx_roll *r, const char *s);
/* New text at time now: returns 1 when it differs from the current one (cells to redraw). */
int nx_roll_set(nx_roll *r, const char *s, uint32_t now);
uint16_t nx_roll_cell_p(const nx_roll *r, unsigned i, uint32_t now);   /* eased; 65535 = settled */
unsigned nx_roll_cell_tint(const nx_roll *r, unsigned i, uint32_t now); /* 0..32 towards the accent */
int nx_roll_active(nx_roll *r, uint32_t now);                            /* anything still moving/tinted */
/* Draws the rolling text with font f at (x, y): each cell clipped to its box, c_text tinted
 * towards c_accent. */
void nx_roll_draw(nx_gc *gc, const nx_roll *r, const nx_font_t *f, int16_t x, int16_t y, uint32_t now,
                  uint16_t c_text, uint16_t c_accent);
