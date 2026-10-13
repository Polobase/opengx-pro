/* Built-in presets with sweeps (NX_FEAT_PRESET; docs/nx/features.md, "Presets with sweeps"): the
 * presets of the table in nx_preset_data.h as program sources (nx_src.h, NX_SRC_PRESET + n),
 * played as Spooky2 plays them on a GX Pro.
 *
 * Loading fills the program like any source (the wire code of each step's start; a step's time
 * from nx_preset_dwell_s). Before its first step plays, the generator gets what Spooky2 sets up
 * for the preset: its two wave tables (":aNN=" and 1024 samples each: `cycles` sine cycles at
 * half scale for Out1, the same inverted for Out2), then, and again whenever it had stopped or
 * paused (the runner's idle configuration undoes them), the lines that make Out2 the inverse at
 * its own offset (:w17=0,0, :w21, :w33). The program's clock waits meanwhile (g_nx_preset_hold). While a sweep step plays, one :w24 line
 * goes out every NX_SWEEP_TICK_MS (a waiting one is replaced by the newer), as for the manual
 * generator. A table the generator does not answer stops the preset (NXE_NO_TABLE).
 *
 * The preset leaves its sine in the generator's table 11, as Spooky2 does with its presets. So
 * the other built-in sources that play table 11 (the manual generator's and the Terrain sets'
 * "Sine") get Spooky2's standard sine there first, once: nx does not know what the table holds
 * after power-on, after a PC session or after a preset. Stored slots play their own tables
 * (41-70) and are not touched. */
#pragma once

#include <stdint.h>

#include "nx_preset_data.h"
#include "nx_runner.h"
#include "state.h"

#define NX_PRESET_SAMPLES 1024u

unsigned nx_preset_count(void);
const char *nx_preset_name(unsigned n);                 /* "" when there is no such preset */
/* Preset n's steps, with NX_PRESET_LOOP set when it repeats until stopped; 0: no such preset. */
#define NX_PRESET_LOOP 0x100u
unsigned nx_preset_info(unsigned n);
uint32_t nx_preset_dwell_s(unsigned n, unsigned i);     /* time of step i (0-based), seconds, at least 1 */
int nx_preset_fill(unsigned n, struct program *p);      /* NX_OK, or NX_EINVAL */
/* Sample i of a wave table, as Spooky2 uploads it. cycles 0: its standard sine, one cycle at
 * full scale, floor(512 + 511.5 * sin(2 pi i / 1024)) (511 at i = 512). cycles n: a preset's, n
 * sine cycles at half scale (256..767), floor(512 + 255.75 * sin(2 pi n i / 1024)); inv:
 * inverted (1023 - it). */
unsigned nx_preset_sample(unsigned cycles, unsigned i, int inv);

/* For the runner (nx_runner.c). */
extern volatile uint8_t g_nx_preset_hold;   /* bit ch: its tables and lines go out first: its clock waits */
void nx_preset_loaded(unsigned ch);      /* a program was loaded into ch: a preset's tables are due */
int nx_preset_setup(unsigned ch);        /* 1: ch may play (what its source needs of tables and lines is out) */
/* The wire code of ch's preset now (its step and the time in it): frequency / cycles rounded to
 * 1e-8 Hz, trailing zeros in the exponent digit (36 Hz in 11 cycles: 3272727270; 396 Hz: 368).
 * 0 when ch plays no preset. */
uint64_t nx_preset_code(unsigned ch);
/* The dashboard's frequencies of ch's preset: the output frequency of its last line (the program
 * holds it / cycles) into *freq, the next step's start into *next when it is set. */
void nx_preset_shown(unsigned ch, const struct nx_run_times *t, uint64_t *freq, uint64_t *next);

void nx_preset_boot(void);                /* its state as after power-on (the host tests; the image starts so) */
void nx_preset_task(uint32_t now);       /* main loop, after the runner: feeds the tables, the sweep's lines */
