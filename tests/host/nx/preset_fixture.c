/* Made-up presets with sweeps for the host tests (the real one, generated from a Spooky2 preset
 * by tools/gen_nx_preset.py, is src/nx/data/nx_preset_data.c): the same shapes with invented
 * names, frequencies and short times. */
#include "nx_preset_data.h"

#define F(c) { (uint8_t)((c) & 0xFFu), (uint8_t)(((c) >> 8) & 0xFFu), (uint8_t)((c) >> 16) }

static const struct nx_pstep k_step[] = {
    /* "Demo sweep": 11 cycles per table, repeats */
    { F(11000), F(121000), 10 },   /* 110.00 -> 1210.00 Hz in 10 s */
    { F(49500), F(49500), 4 },     /* 495.00 Hz for 4 s */
    { F(220000), F(55000), 5 },    /* 2200.00 -> 550.00 Hz in 5 s (downwards) */
    /* "Demo once": 3 cycles per table, plays once */
    { F(1234), F(1234), 2 },       /* 12.34 Hz for 2 s */
    { F(30000), F(60000), 3 },     /* 300.00 -> 600.00 Hz in 3 s */
};

static const struct nx_preset k_preset[] = {
    { 0, 0, 3, 11, 11, 25, 1000, 20, 220, 1, 0 },
    { 11, 3, 2, 3, 11, 25, 1000, 20, 220, 0, 0 },
};

const struct nx_pdata k_nx_presets = { k_step, k_preset, "Demo sweep\0Demo once", 2 };
