/* Session timers (menu > Timers, NX_FEAT_TIMERS).
 *
 * Stop after: g_nx_settings.session_limit_min[ch] minutes of running (paused time does not
 * count; a stop, and a changed limit, start the count again) stop the generator as its STOP key
 * would (NXA_STOP), and the UI gets NXE_TIMER_STOP.
 *
 * Power off when all finished (g_nx_settings.off_when_done): once a program has ended by itself
 * (or by its timer) and no generator runs or is paused any more, the UI gets NXE_ALL_DONE and
 * shows a countdown card before it asks for the power-off (NXA_POWER_OFF). Starting a program
 * again cancels the pending offer.
 *
 * Nothing counts in PC mode, in calibration or before the boot gate. */
#pragma once

#include <stdint.h>

void nx_timer_reset(void);
void nx_timer_task(uint32_t now_ms);   /* main loop, after the runner and the link work */
void nx_timer_finished(unsigned ch);   /* runner: ch's program ended by itself (not looped) */
void nx_timer_goes_on(void);           /* a chain started its next program: not "all finished" */
uint32_t nx_timer_left_ms(unsigned ch);   /* rest of ch's "stop after" time, 0 = none / not counting */
