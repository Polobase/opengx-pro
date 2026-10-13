/* nx program runner for the stored slots 1..30 (docs/nx/fw2-design.md, "Runner v2", plain slot
 * playback). It keeps the classic runner's state in g_ch[] and g_ui_flags (loaded program =
 * upload buffer, step, step_sent, RUN/LOOP; docs/nx/design/firmware-boundary.md C.5 item 7), so
 * the PC-visible couplings stay the original's, and it sends through runner_seq, so every link
 * gets the same lines in the same order as src/classic/runner.c (proven in
 * tests/host/nx/test_nx_runner.c).
 *
 * Documented nx differences (DEVIATIONS.md, "nx"):
 *   - g_ch[].elapsed counts milliseconds (SysTick), so a step lasts exactly dwell x 1.00 s
 *     (the original counts 10.1 ms units, so its steps last dwell x 1.01 s);
 *   - G1 and G2 are planned independently and sent in parallel through nx_txq, each link
 *     waiting for its own :ok (the original sends G1's sequence, then G2's);
 *   - a count above 200 ends after step 200 (the original reads past the frequency table).
 * After a PC session the channel's program buffer is what the PC last uploaded into it, and the
 * upload's forced resend (step_sent = 0xFF) sends that buffer's idle configuration, exactly as the
 * original does (scenario pc_upload_exit). */
#pragma once

#include <stdint.h>

#ifndef NX_DWELL_UNITS
#define NX_DWELL_UNITS 1000u   /* elapsed units per second of dwell (ms); a test build: make NX_TIME_X=60 */
#endif
#if defined(NX_RELEASE) && NX_RELEASE && NX_DWELL_UNITS != 1000u
#error "release build with test timing (NX_TIME_X)"
#endif

/* Main loop: for each link whose queue is idle, work out the step and queue its lines (the
 * original's runner_tick, per link). Does nothing in PC mode, in calibration mode, or while a
 * PC-mode entry/exit waits for the queues to drain. */
void nx_runner_task(void);
void nx_runner_tick_1ms(void);   /* SysTick: elapsed++ of every running channel */
void nx_runner_loaded(unsigned ch);   /* NXA_LOAD: ch's idle configuration goes out before its first step */

/* Where channel ch stands, from the same millisecond arithmetic that plays the steps (the UI's
 * step counter, progress bar and time left; docs/nx/design/ui.md §6.4 item 6). A stopped or fresh
 * channel is at step 1 with the whole program left. */
struct nx_run_times {
    uint32_t step, count;       /* step 1..count (0 when count is 0) */
    uint32_t dwell_ms;          /* per step (a dwell of 0 counts as 1 s, as the runner plays it) */
    uint32_t total_ms, elapsed_ms, left_ms, step_left_ms;
};
void nx_runner_times(unsigned ch, struct nx_run_times *t);

/* 1 when a blocking SPI-flash write cannot delay a generator line: both queues idle and no
 * running program reaches its next step within need_ms (settings and resume writes). */
int nx_runner_quiet(uint32_t need_ms);
