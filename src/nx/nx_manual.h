/* The manual generator (menu > Manual, NX_FEAT_MANUAL): one frequency, or a sweep, with a
 * waveform and an amplitude, played as the source NX_SRC_MANUAL (nx_src.h) on one generator.
 *
 * Playing loads it like a program of one step (the idle configuration with its waveform table
 * and amplitude, then the step's lines) that does not end. A sweep sends only :w24 lines to that
 * generator, one every NX_SWEEP_TICK_MS while its queue is idle (a queued one is replaced by the
 * newer, NX_TXQ_COALESCE): linear or logarithmic from start to end over its time, once (then it
 * ends as a program does), again and again, or back and forth. Changes apply while it plays: a
 * frequency with one :w24 line, a waveform or amplitude with the outputs off, the new idle
 * configuration, and on again. The values are kept in the KV store (NX_KEY_MANUAL), written when
 * quiet and never in PC mode. */
#pragma once

#include <stdint.h>

#include "state.h"

#define NX_KEY_MANUAL 0x0008u
#define NX_MANUAL_VER 1u
#define NX_MANUAL_MAX_CHZ 4000000000u   /* 40 MHz in 0.01 Hz */
#define NX_SWEEP_TICK_MS 125u           /* Spooky2's own sweeps: about 134 ms */

enum { NX_MAN_FIXED, NX_MAN_SWEEP };
enum { NX_SW_ONCE, NX_SW_LOOP, NX_SW_PINGPONG };

struct nx_manual {
    uint8_t ver, mode;
    uint8_t wave;        /* 1..9: Spooky2 waveform (generator table 10 + wave) */
    uint8_t shape;       /* sweep: 0 linear, 1 logarithmic */
    uint8_t repeat;      /* sweep: NX_SW_* */
    uint8_t rsv;
    uint16_t ampl_dv;    /* 0..200: 0.1 V */
    uint32_t f_chz;      /* the fixed frequency, 0.01 Hz */
    uint32_t start_chz, end_chz;
    uint32_t time_s;     /* sweep time, 1..86400 s */
};
extern struct nx_manual g_nx_manual;

void nx_manual_boot(void);                    /* after nx_store_mount */
int nx_manual_fill(struct program *p);        /* the one-step program of the values */
int nx_manual_play(unsigned ch);              /* load and play on ch: NX_OK or the core's refusal */
void nx_manual_changed(void);                 /* g_nx_manual changed: a generator playing it follows */
uint32_t nx_manual_step(const struct channel *c);   /* the runner's step: 1, or 2 once a sweep ran once */
uint32_t nx_manual_period_ms(void);           /* a sweep's time, 0 for a fixed frequency */
uint32_t nx_manual_chz(uint32_t el_ms);       /* the frequency el_ms into the run */
void nx_manual_task(uint32_t now);            /* main loop, after the runner */
