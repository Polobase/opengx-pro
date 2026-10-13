/* Built-in presets with sweeps (docs/nx/features.md, "Presets with sweeps"): generated from a
 * Spooky2 preset by tools/gen_nx_preset.py into src/nx/data/nx_preset_data.c (committed;
 * `make nx-data` writes it again, `make APP=nx NX_PRESETS=0` builds without), and made up for the
 * host tests (tests/host/nx/preset_fixture.c).
 *
 * A preset is a list of steps, each with its own time: a fixed frequency, or a linear sweep
 * from a to b over that time. It plays a wave table of `cycles` sine cycles (Spooky2's Wave
 * Cycle Multiplier), so the generator gets the frequency / cycles. */
#pragma once

#include <stdint.h>

struct nx_pstep {
    uint8_t a[3];      /* centi-Hz, little-endian: the frequency, or the start of the sweep */
    uint8_t b[3];      /* the end of the sweep; equal to a: a fixed frequency */
    uint16_t dwell;    /* seconds, at least 1 */
};

struct nx_preset {
    uint16_t name;     /* offset of its name in str */
    uint16_t step;     /* its first step */
    uint8_t n;         /* steps (at most 200) */
    uint8_t cycles;    /* sine cycles in its wave tables, at least 1 */
    uint8_t wave1;     /* generator table of Out1 (:w20): the sine */
    uint8_t wave2;     /* generator table of Out2 (:w21): the same, inverted */
    uint16_t ampl_cv;  /* :w28 / :w29, 0.01 V */
    uint8_t offs1;     /* :w32: 120 + offset in percent */
    uint8_t offs2;     /* :w33 */
    uint8_t loop;      /* 1: it repeats until stopped */
    uint8_t rsv;
};

struct nx_pdata {
    const struct nx_pstep *step;
    const struct nx_preset *preset;
    const char *str;   /* NUL-terminated names */
    uint8_t n;         /* presets */
};

extern const struct nx_pdata k_nx_presets;
