/* Key gesture engine (docs/nx/design/ui.md §5.1). The SysTick handler feeds the raw key mask
 * every NX_KEY_SAMPLE_MS; the engine debounces it (two equal samples) and queues gesture events
 * for the main loop. Pure logic, no hardware: host-tested in tests/host/nx/test_nx_keys.c.
 *
 * Events: PRESS (key down), RELEASE (up, n = held ms), HOLD (500 ms), LONG (1500 ms),
 * REPEAT (keys in the repeat mask: 400 ms, then every 110 ms, every 50 ms from 1.5 s) and
 * CHORD (a second key within 80 ms of the first; the chord then owns HOLD, LONG and RELEASE of
 * its keys, reported with the chord's key mask). Keys in the chord-delay mask report PRESS
 * 60 ms late so that a chord can still form (G1+G2); a shorter tap reports PRESS at release.
 * POWER held NX_KEY_POWER_OFF_MS sets the power-off request, whatever else is going on: the
 * core, not the UI, decides to power off. A key already down at the very first sample fed after
 * nx_keys_reset() (POWER is usually still down at power-on, since POWER is the key that turns the
 * board on) is ignored until it is released once, so it cannot start a PRESS, a chord, a hold
 * timer or the power-off hold from before this engine started sampling. */
#pragma once

#include <stdint.h>

#include "nx_hw.h"

#define NX_KEY_HOLD_MS 500u
#define NX_KEY_LONG_MS 1500u
#define NX_KEY_REPEAT_FIRST_MS 400u
#define NX_KEY_REPEAT_SLOW_MS 110u
#define NX_KEY_REPEAT_FAST_MS 50u
#define NX_KEY_REPEAT_ACCEL_MS 1500u
#define NX_KEY_CHORD_MS 80u
#define NX_KEY_CHORD_DELAY_MS 60u
#define NX_KEY_POWER_OFF_MS 800u
#define NX_KEY_QUEUE 16u

enum nx_kev_type { NXK_PRESS = 1, NXK_RELEASE, NXK_HOLD, NXK_LONG, NXK_REPEAT, NXK_CHORD };

struct nx_kev {
    uint8_t type;   /* enum nx_kev_type */
    uint8_t keys;   /* one NX_KEY_* bit, or all keys of a chord */
    uint16_t n;     /* RELEASE: held ms (65535 at most); REPEAT: 1, 2, 3, ... */
    uint32_t t;     /* ms at which the event happened */
};

/* The act-on rule (ui.md §5.1): a key acts on PRESS (and on REPEAT), unless it has a hold
 * meaning in the current context; then it acts on a RELEASE before NX_KEY_HOLD_MS (SHORT) or on
 * HOLD. Returns what a single-key event means for such a key. */
enum nx_kact { NXKA_NONE, NXKA_SHORT, NXKA_HOLD };
enum nx_kact nx_keys_act(const struct nx_kev *e, int has_hold);

void nx_keys_reset(void);   /* no keys down, queue empty, default masks, no power-off request;
                              * re-arms the boot ignore-until-release mask */
/* Keys that auto-repeat (default UP, DOWN) and keys whose PRESS waits for a chord (default G1,
 * G2); set by the UI per context. */
void nx_keys_config(uint8_t repeat_mask, uint8_t chord_delay_mask);
/* Repeat timing (Settings -> Keys & lock): first REPEAT after first_ms, then every slow_ms until
 * NX_KEY_REPEAT_ACCEL_MS, then every NX_KEY_REPEAT_FAST_MS. nx_keys_reset restores 400 / 110. */
void nx_keys_set_repeat(uint16_t first_ms, uint16_t slow_ms);
void nx_keys_feed(uint8_t raw, uint32_t now_ms);   /* SysTick, every NX_KEY_SAMPLE_MS */
int nx_keys_pop(struct nx_kev *e);                 /* main loop: 1 = *e is the next event */
void nx_keys_flush(void);                          /* main loop: drop the queued events */
int nx_keys_pending(void);                         /* events waiting */
uint8_t nx_keys_down(void);                        /* debounced keys that are down */
int nx_keys_power_off_due(void);                   /* POWER held NX_KEY_POWER_OFF_MS (stays set) */
uint32_t nx_keys_dropped(void);                    /* events lost because the queue was full */
