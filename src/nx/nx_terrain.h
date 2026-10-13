/* Built-in Terrain Protocol (docs/nx/features.md, "Terrain Protocol"): the sets of the tables in
 * nx_terrain_data.h as program sources (nx_src.h, NX_SRC_SET + set). */
#pragma once

#include <stdint.h>

#include "nx_terrain_data.h"
#include "state.h"

unsigned nx_tset_steps(unsigned set);                /* steps of set (end step included); 0: no such set */
uint32_t nx_tset_dwell_s(unsigned set, unsigned i);  /* dwell of step i (0-based), seconds, at least 1 */
/* p = set as a program: its waveform table, amplitude (ampl_cv, or the set's own when 0), offset
 * 120, the first step's dwell, the step count and the frequency codes (centi-Hz c: c * 10 + 6). */
int nx_tset_fill(unsigned set, uint16_t ampl_cv, struct program *p);
const char *nx_tset_name(unsigned set);              /* "" when there is no such set */

/* ---- the protocols (NX_FEAT_TERRAIN) ----
 *
 * Remote (NX_TP_REMOTE): each item's set plays looped; once it has run its hours (running time:
 * a pause or a power cut does not count), the next item's set is loaded and played, and after the
 * last item the chain starts again at day 1, as the Spooky2 preset does (Repeat_Chain=0). The
 * power level sets the amplitude (the low-power presets are the same data at 2.5 / 1 / 0.5 /
 * 0.25 V).
 * Contact (NX_TP_CONTACT): each day has two sessions; playing plays the session's set once. When
 * its 0 Hz end step starts (Spooky2's "safe to pause" notification), the session counts as done:
 * the position moves on and the UI gets NXE_ALERT. After the last session the protocol is
 * complete; playing it again starts at day 1.
 * Stopping keeps the position; the next play goes on from there. Loading another program, PC
 * mode or a failed load end the run. The positions are kept in the KV store (NX_KEY_TERRAIN),
 * written like the chains (quiet, never in PC mode); the resume snapshot (nx_resume.h) keeps a
 * running protocol across a power cut. */
#define NX_KEY_TERRAIN 0x0007u
#define NX_TERRAIN_VER 1u
#define NX_TERRAIN_MAX 6u      /* protocols with a stored position */
#define NX_TERRAIN_POWERS 5u   /* Remote: 5 (the set's own), 2.5, 1, 0.5, 0.25 V */
#define NX_TERRAIN_SAVE_MS 300000u   /* a running Remote item's time is written this often */

struct nx_tpos {
    uint8_t item;    /* Remote: chain item; Contact: session (day * 2 + 0/1), days * 2 = complete */
    uint8_t power;   /* Remote: 0..NX_TERRAIN_POWERS - 1 */
    uint16_t rsv;
    uint32_t run_s;  /* Remote: running time in the item */
};

struct nx_terrain_rec {
    uint8_t ver, rsv;
    uint8_t active[2];   /* the protocol each generator played last (1-based, 0 = none): auto-start */
    struct nx_tpos p[NX_TERRAIN_MAX];
};
extern struct nx_terrain_rec g_nx_terrain;

/* What a generator plays: id 1..nx_terrain_count(), 0 = none. */
struct nx_trun {
    uint8_t id, item, power;
    uint32_t run_s;
};

unsigned nx_terrain_count(void);
const struct nx_tproto *nx_terrain_proto(unsigned id);   /* 0 when there is no such protocol */
unsigned nx_terrain_items(unsigned id);                   /* Remote: items; Contact: days * 2 */
unsigned nx_terrain_set_of(unsigned id, unsigned item);   /* the item's set */
uint16_t nx_terrain_ampl(unsigned power);                  /* 0.01 V, 0 = the set's own */
const char *nx_terrain_name(unsigned id);                 /* "Terrain", ...; "" when there is none */
/* Where item stands: its day (1-based), part (Remote: 0 the whole day, 1/2 its halves; Contact:
 * session 1/2) and hours (Remote; Contact: 0). */
void nx_terrain_where(unsigned id, unsigned item, uint8_t *day, uint8_t *part, uint8_t *hours);
void nx_terrain_boot(void);                               /* after nx_store_mount */
int nx_terrain_play(unsigned ch, unsigned id);            /* from its position: NX_OK, NX_EINVAL, refusal */
int nx_terrain_continue(unsigned ch);                     /* auto-start: the protocol ch played last */
void nx_terrain_move(unsigned id, int delta);             /* the position one item on or back (wraps) */
void nx_terrain_power(unsigned id);                       /* the next power level (Remote) */
void nx_terrain_finished(unsigned ch);                    /* runner: ch's program ended by itself */
void nx_terrain_task(uint32_t now);                       /* main loop, after the chains */
void nx_terrain_run(unsigned ch, struct nx_trun *r);
void nx_terrain_resume(unsigned ch, const struct nx_trun *r);   /* the resume snapshot's run */
