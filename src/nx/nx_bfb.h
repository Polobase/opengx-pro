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
 *   4. NX_BFB_BASELINE reads of both inputs (:r11, :r12);
 *   5. the sweep: a :w24 line every NX_BFB_STEP_MS (Spooky2's "Delay for GX Inputs"), each
 *      0.025 % above the one before, 41 kHz to 1.8 MHz in NX_BFB_STEPS steps (about 18 min),
 *      NX_BFB_SETTLE_MS after its :ok the two reads.
 * A read's reply (:r11=<value>.) is taken from the receive tap and answers the queued line as an
 * :ok does; a read without a reply is a missed step (the PC marks it a dropout).
 *
 * Hits are found as Spooky2 finds them (the detection of Spooky.exe, decoded in the Hunt & Kill
 * port this comes from): the :r11 reading against the average of the NX_BFB_WINDOW readings
 * before it; a local maximum (a rise, a run of equal readings, a fall) with a deviation above
 * zero is a hit at the frequency of the step after its first reading; the NX_BFB_HITS largest
 * deviations are kept, the earlier one first on a tie. Scoring begins once the window has
 * settled (its spread, and the reading, within 1 % of its mean), at most 5 windows in, else at
 * the first full window. The PC scores the recorded sweep afterwards; this scores as the
 * readings come, so a hit in the first steps before the settle is known is not scored.
 *
 * The hits become a program in slot `slot` (the sine, the target amplitude, dwell_s per hit,
 * named "Biofeedback G1" / "G2") that is loaded and played on that generator: Spooky2's "kill".
 * Without hits the generator stops. STOP, PC mode or another program on that generator end a
 * scan with the idle configuration (outputs off). */
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
};

struct nx_bfb_cfg {
    uint8_t ch;          /* the generator, 0 = G1 */
    uint8_t slot;        /* the program the hits become, 1..30 */
    uint16_t ampl_cv;    /* target amplitude, 0.01 V peak to peak (Spooky2: 20 V) */
    uint16_t dwell_s;    /* per hit when the hits play (Spooky2: 180 s) */
    uint8_t speed;       /* 0 standard, 1 balanced, 2 fast (NX_BFB_SPEED_*) */
};

struct nx_bfb_hit {
    uint64_t code;       /* the frequency's :w24 code (state.h) */
    uint32_t dev20;      /* reading minus the running average, in 1/20 of a reading unit */
    uint16_t step;       /* the sweep step of that frequency (1-based): the graph's column */
};

struct nx_bfb_view {   /* what the screen shows */
    uint8_t state;       /* enum nx_bfb_state */
    uint8_t ch;
    uint8_t nhits;
    uint8_t slot;        /* NX_BFB_DONE: the slot the hits are in */
    uint16_t step;       /* sweep steps done */
    uint16_t missed;     /* reads without a reply */
    uint32_t reading;    /* the last :r11 value */
    uint32_t sweep_ms;   /* nx_ms() when the sweep began: the screen's time left from the pace so far */
    uint16_t steps;      /* of the sweep, at the speed chosen (NX_BFB_SPEED_STEPS) */
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
void nx_bfb_task(uint32_t now);         /* main loop, after the runner */
void nx_bfb_rx(unsigned ch, uint8_t c); /* LINK_RX_TAP: the generator's :rNN=<v>. replies (interrupt) */
int nx_bfb_fill(struct program *p);     /* the one-step program the scan starts with */
const struct nx_bfb_view *nx_bfb_view(void);

/* Pure, host-tested. The vendor's token for the generator's 9-digit response to a 9-digit
 * challenge (digits 1..9); tok gets 9 digits and a NUL. */
void nx_bfb_auth_token(const char *chal, const char *resp, char *tok);
/* The :w24 code of f8 (1e-8 Hz): trailing zeros into the exponent digit, as Spooky2 sends it
 * (41010.25 Hz: 41010256). */
uint64_t nx_bfb_code(uint64_t f8);
