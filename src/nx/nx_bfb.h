/* Biofeedback scan on the device (menu > Biofeedback, NX_FEAT_BFB): Spooky2's GX biofeedback
 * scan, run by the display on one generator without a PC, its hits stored as a program and
 * played.
 *
 * The sequence is Spooky2's (its serial captures: docs/serial-commands.md §4, BACKLOG.md):
 *   1. the generator's session, opened with the vendor handshake (:r90=<challenge>, answered
 *      :r90=<echo>,<response>., then :w92=<token>. answered :ok). The generator may not need
 *      it for lines from the display (standalone mode, PB2 high); a challenge it does not
 *      answer is skipped;
 *   2. the sine at 41 kHz as a one-step program (NX_SRC_BFB): the idle configuration, the
 *      standard sine into table 11 (nx_preset.h), the step with the outputs on at the first
 *      ramp value;
 *   3. the amplitude raised to the target in NX_BFB_RAMP_STEPS steps (:w28 and :w29);
 *   4. NX_BFB_BASELINE reads of both inputs (:r11, :r12), a pair per step time as Spooky2 paces
 *      them (about 15 s: the reading's level settles before the window is seeded);
 *   5. the sweep: a :w24 line every NX_BFB_STEP_MS (Spooky2's "Delay for GX Inputs"), each
 *      0.025 % above the one before, 41 kHz to 1.8 MHz in NX_BFB_STEPS steps (about 18 min),
 *      NX_BFB_SETTLE_MS after its :ok the two reads.
 * A read's reply (:r11=<value>.) is taken from the receive tap and answers the queued line as an
 * :ok does; a read without a reply is a missed step (the PC marks it a dropout).
 *
 * Hits are found as Spooky2 finds them (the detection of Spooky.exe, decoded in the Hunt & Kill
 * port this comes from): the :r11 reading against the average of the NX_BFB_WINDOW readings
 * before it; a local maximum (a rise, a run of equal readings, a fall) with a deviation above
 * zero is a hit at the frequency of its first reading (Spooky2's own hits on a recorded scan come
 * out the same, test_nx_bfb.c); the NX_BFB_HITS largest
 * deviations are kept, the earlier one first on a tie. Scoring begins once the window has
 * settled (its spread, and the reading, within 1 % of its mean), at most 5 windows in, else at
 * the first full window. The PC scores the recorded sweep afterwards; this scores as the
 * readings come and drops the hits it has when the window settles, which comes to the same.
 *
 * The hits become a program in slot `slot` (the sine, the target amplitude, dwell_s per hit,
 * named "Biofeedback G1" / "G2") that is loaded and played on that generator, looped: Spooky2's
 * "kill". With Refine on it plays once, as the next pass follows its end (the last possible pass
 * plays looped).
 * Without hits the generator stops and its one-step program is unloaded; STOP does the same, and
 * PC mode or another program on that generator end a scan as well. What it found stays in the
 * view (NX_BFB_STOPPED) until the next scan. START on the screen pauses the sweep: no step goes out,
 * the generator holds the frequency reached with the outputs on (so going on needs no ramp), the
 * pace excludes the pause; START again goes on.
 *
 * Refine (cfg.refine): when the hits' program ends by itself (not looped), the scan runs again
 * on the hits alone, Spooky2's "Continue Refining Hits" (Spooky.exe, decoded in the Hunt & Kill
 * port): the step halves each pass (to f / 2^24, a pass at that step the last) and each hit is
 * swept from 10 steps below it to 10 above,
 * overlapping windows as one, after the same session, ramp and baseline (at the first window);
 * the new hits replace the old, the program in the slot is written again and plays, and the
 * next pass follows its end, until a pass finds no hits or the program is stopped or replaced.
 * The scan screen comes up for a pass (NXE_BFB_REFINE); the view keeps the hits and the graph
 * of the pass before until its sweep begins. */
#pragma once

#include <stdint.h>

#include "state.h"

#define NX_BFB_HITS 10u
#define NX_BFB_WINDOW 20u       /* readings in the running average (BFB_RA_Window_1) */
#define NX_BFB_RAMP_STEPS 330u  /* amplitude steps up to the target */
#define NX_BFB_BASELINE 203u    /* reads before the sweep */
#define NX_BFB_STEP_MS 70u      /* per sweep step, :w24 to :w24 */
#define NX_BFB_SETTLE_MS 25u    /* :ok of the :w24 to the first read */
#define NX_BFB_START_DELAY_MS 200u
#define NX_BFB_START_F8 4100000000000ull   /* 41 kHz in 1e-8 Hz */
#define NX_BFB_END_F8 180000000000000ull   /* 1.8 MHz */
#define NX_BFB_STEPS 15130u     /* the grid of 0.025 % steps from the start past the end (host-tested) */
#define NX_BFB_STEPS2 7566u     /* the 0.05 % grid of the quicker speeds (host-tested) */
#define NX_BFB_FAST_MS 45u      /* per sweep step at the fast speed */
/* The speed (nx_bfb_cfg.speed): 0 standard, Spooky2's grid and pace; 1 balanced, every other
 * frequency; 2 fast, and 45 ms a step. The responses are 6 to 20 standard steps wide (the Android
 * app's phone logs), so the coarser grid still finds them. The sweep's steps, grid (f += f / grid)
 * and pace at a speed: */
#define NX_BFB_SPEEDS 3u
#define NX_BFB_SPEED_STEPS(sp) ((sp) ? NX_BFB_STEPS2 : NX_BFB_STEPS)
#define NX_BFB_SPEED_GRID(sp) ((sp) ? 2000u : 4000u)
#define NX_BFB_SPEED_MS(sp) ((sp) == 2u ? NX_BFB_FAST_MS : NX_BFB_STEP_MS)
#define NX_BFB_NAME "Biofeedback"
#define NX_BFB_TRACE 296u       /* columns of the scan graph: the screen's width less its margins */

enum nx_bfb_state {
    NX_BFB_IDLE = 0,
    NX_BFB_AUTH,       /* the session handshake */
    NX_BFB_LOAD,       /* the program loads and plays: its idle configuration, the sine, the step */
    NX_BFB_RAMP,
    NX_BFB_BASE,
    NX_BFB_SWEEP,
    NX_BFB_SAVE,       /* the hits into the slot, which then plays */
    NX_BFB_DONE,       /* hits (view.nhits) play from the slot, or there were none */
    NX_BFB_FAILED,     /* no readings at all: the generator answers no :r11 (no session, no sensor) */
    NX_BFB_STOPPED,    /* ended before its end: STOP, PC mode, another program (view.step: how far it came) */
};

struct nx_bfb_cfg {
    uint8_t ch;          /* the generator, 0 = G1 */
    uint8_t slot;        /* the program the hits become, 1..30 */
    uint16_t ampl_cv;    /* target amplitude, 0.01 V peak to peak (Spooky2: 20 V) */
    uint16_t dwell_s;    /* per hit when the hits play (Spooky2: 180 s) */
    uint8_t speed;       /* 0 standard, 1 balanced, 2 fast (NX_BFB_SPEED_*) */
    uint8_t refine;      /* 1: a pass on the hits alone follows the end of their program */
};

struct nx_bfb_hit {
    uint64_t code;       /* the frequency's :w24 code (state.h) */
    uint32_t dev20;      /* reading minus the running average, in 1/20 of a reading unit */
    uint16_t step;       /* the sweep step of that frequency, its peak reading's (1-based): the graph's column */
    uint16_t live;       /* that reading's count in the live ring (view.live_n as it went in): the live strip's mark */
};

struct nx_bfb_view {   /* what the screen shows */
    uint8_t state;       /* enum nx_bfb_state */
    uint8_t ch;
    uint8_t nhits;
    uint8_t slot;        /* NX_BFB_DONE: the slot the hits are in */
    uint8_t pass;        /* 1: the sweep of the whole range; 2 and up: a refining pass on the hits */
    uint8_t playing;     /* NX_BFB_DONE: the hits' program is the one loaded, until it is stopped or replaced */
    uint8_t paused;      /* NX_BFB_SWEEP: START paused it */
    uint16_t step;       /* sweep steps done */
    uint16_t missed;     /* reads without a reply */
    uint32_t reading;    /* the last :r11 value */
    uint32_t sweep_ms;   /* nx_ms() when the sweep began (moved on by the pauses): the screen's time left from the pace so far */
    uint32_t pause_ms;   /* nx_ms() when the pause began */
    uint16_t steps;      /* of the sweep, at the speed chosen (NX_BFB_SPEED_STEPS) */
    uint16_t prog;       /* NX_BFB_RAMP: amplitude steps sent of NX_BFB_RAMP_STEPS; NX_BFB_BASE: pairs read of NX_BFB_BASELINE */
    uint64_t code;       /* the sweep frequency now, as a :w24 code */
    struct nx_bfb_hit hits[NX_BFB_HITS];
    /* the scan graph: per column of the sweep the largest deviation above the running average
     * among its readings (reading units, saturating), and the last NX_BFB_TRACE readings'
     * deviations (clipped to +-127) as a ring, live_n pushed so far */
    uint8_t trace[NX_BFB_TRACE];
    int8_t live[NX_BFB_TRACE];
    uint16_t live_n;
};

extern struct nx_bfb_cfg g_nx_bfb_cfg;

void nx_bfb_boot(void);                 /* power-on state */
int nx_bfb_start(void);                 /* NX_OK, NX_BUSY (a scan runs, the slot is loaded elsewhere, the core) */
void nx_bfb_stop(void);                 /* ends a scan: the generator's idle configuration */
void nx_bfb_pause(uint32_t now);        /* the sweep: paused, or on again */
void nx_bfb_task(uint32_t now);         /* main loop, after the runner */
void nx_bfb_finished(unsigned ch);      /* runner: ch's program ended by itself (a refining pass may follow) */
void nx_bfb_rx(unsigned ch, uint8_t c); /* LINK_RX_TAP: the generator's :rNN=<v>. replies (interrupt) */
int nx_bfb_fill(struct program *p);     /* the one-step program the scan starts with */
const struct nx_bfb_view *nx_bfb_view(void);

/* Pure, host-tested. The vendor's token for the generator's 9-digit response to a 9-digit
 * challenge (digits 1..9); tok gets 9 digits and a NUL. */
void nx_bfb_auth_token(const char *chal, const char *resp, char *tok);
/* The :w24 code of f8 (1e-8 Hz): trailing zeros into the exponent digit, as Spooky2 sends it
 * (41010.25 Hz: 41010256). */
uint64_t nx_bfb_code(uint64_t f8);
