/* Waveform preview "scope" (docs/nx/design/ui.md §2.9): the shape of a generator's output, not to
 * scale: 2.5 periods across the plot, normalised, with gating bands, Out2 (sync / inverse /
 * independent) and labels. nx_wave_sample is the pure core (host-tested); nx_scope_draw renders
 * one card with nx_trace (no sample buffers: the samples are computed per column). The phase
 * advances 0.5 cycles/s in 12 fps steps while running; paused scopes freeze in text2, stopped
 * ones show a flat line; reduced motion keeps the phase at 0. */
#pragma once

#include <stdint.h>

#include "nx_gfx.h"

enum nx_wave {
    NX_WAVE_FLAT = 0,
    NX_WAVE_SINE,
    NX_WAVE_SQUARE,
    NX_WAVE_SAW,
    NX_WAVE_INV_SAW,
    NX_WAVE_TRIANGLE,
    NX_WAVE_DAMPED_SINE,
    NX_WAVE_DAMPED_SQUARE,
    NX_WAVE_HBOMB_EDGE,   /* approximation */
    NX_WAVE_HBOMB_SINE,   /* approximation */
    NX_WAVE_CUSTOM,       /* 64 samples, -127..127 */
    NX_WAVE_COUNT,
};

enum nx_out2 { NX_OUT2_OFF = 0, NX_OUT2_SYNC, NX_OUT2_INVERSE, NX_OUT2_INDEPENDENT };
enum nx_scope_state { NX_SCOPE_RUNNING = 0, NX_SCOPE_PAUSED, NX_SCOPE_STOPPED };

#define NX_SCOPE_FRAME_MS 83u        /* 12 fps */
#define NX_SCOPE_RATIO_MIN 64u       /* Out2 / Out1 shown between 0.25 ... */
#define NX_SCOPE_RATIO_MAX 1024u     /* ... and 4 (Q8); the label gives the real ratio */

typedef struct {
    uint8_t wave, wave2;          /* Out1 shape; Out2 shape when independent */
    uint8_t out2;                 /* enum nx_out2 */
    uint8_t state;                /* enum nx_scope_state */
    uint8_t gate_pct;             /* 0: no gating; else the on-time in % */
    uint8_t accent;               /* palette token of the traces: NX_PAL_G1 / NX_PAL_G2 */
    uint8_t unverified;           /* wave code without a verified shape: "wave #N ?" */
    uint8_t wave_code;
    uint16_t ratio_q8;            /* Out2 / Out1 frequency, Q8 (independent) */
    uint16_t ampl_cv;             /* amplitude label in 0.01 V (0: none) */
    const int8_t *custom, *custom2;   /* NX_WAVE_CUSTOM tables (64 samples) */
} nx_scope_cfg;

/* One sample of a shape at phase ph (Q16 of one period), Q12 (-4096..4096). */
int32_t nx_wave_sample(uint8_t wave, const int8_t *custom, uint32_t ph);
/* Phase for time t_ms: 0.5 cycles/s, quantised to NX_SCOPE_FRAME_MS; 0 with reduced motion. */
uint16_t nx_scope_phase(uint32_t t_ms);
void nx_scope_draw(nx_gc *gc, nx_rect r, const nx_scope_cfg *cfg, uint16_t phase);
