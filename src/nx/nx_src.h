/* Program sources (docs/nx/features.md): what a generator's g_ch[].prog_slot names.
 *
 *   1..30             a stored slot (program_load; the original's program list)
 *   NX_SRC_PRESET + n built-in preset n (nx_preset.c): steps with their own time, fixed or sweeps
 *   NX_SRC_MANUAL     the manual generator (nx_manual.c): one step without an end
 *   NX_SRC_SET + n    built-in set n (nx_terrain.c): a frequency list with a dwell per step
 *
 * Loading a source that is not a slot fills g_ch[ch].prog as program_load does (waveform table,
 * amplitude, offset 120, count, frequency codes) with no gating, so the runner sends the same
 * lines as for a slot; only the step times differ. PC-mode entry clears prog_slot
 * (core_pcmode.c), which ends every source, as it ends a slot. */
#pragma once

#include <stdint.h>

#include "state.h"

#define NX_SRC_PRESET 0x30u
#define NX_SRC_PRESETS 15u
#define NX_SRC_MANUAL 0x3Fu
#define NX_SRC_SET 0x40u
#define NX_SRC_SETS 0x40u

static inline int nx_src_is_slot(unsigned id)
{
    return id >= 1u && id <= PROGRAM_SLOTS;
}

static inline int nx_src_is_set(unsigned id)
{
    return id >= NX_SRC_SET && id < NX_SRC_SET + NX_SRC_SETS;
}

static inline int nx_src_is_preset(unsigned id)
{
    return id >= NX_SRC_PRESET && id < NX_SRC_PRESET + NX_SRC_PRESETS;
}

static inline int nx_src_timed(unsigned id)   /* each step has its own time (nx_src_dwell_s) */
{
    return nx_src_is_set(id) || nx_src_is_preset(id);
}

/* Fills c->prog and c->params for a source that is not a slot; ampl_cv 0 keeps the source's own
 * amplitude. NX_OK, or NX_EINVAL for an unknown source. */
int nx_src_fill(unsigned id, uint16_t ampl_cv, struct channel *c);
/* Dwell of step i (0-based) of a timed source id, in seconds (at least 1). */
uint32_t nx_src_dwell_s(unsigned id, unsigned i);
/* Name of a source that is not a slot into out (NUL-terminated, at most NX_UC_NAME_MAX bytes);
 * returns its length, 0 when it has none. */
unsigned nx_src_name(unsigned id, char *out);
