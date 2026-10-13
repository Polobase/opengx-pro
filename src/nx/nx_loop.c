/* The nx main loop (nx_loop.h). */
#include "nx_loop.h"

#include "link.h"
#include "link_internal.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_diag.h"
#include "nx_fault.h"
#include "nx_keys.h"
#include "nx_resume.h"
#include "nx_chain.h"
#include "nx_manual.h"
#include "nx_preset.h"
#include "nx_terrain.h"
#include "nx_hist.h"
#include "nx_runner.h"
#include "nx_slots.h"
#include "nx_timer.h"
#include "nx_time.h"
#include "nx_txq.h"
#include "nx_ui.h"
#include "nx_wdg.h"
#include "state.h"

/* The PB2 hand-back after a session handshake. A :w92 from the PC makes the display raise PB2
 * (the PC cut off) and send :w91=0, to its generator; the generator's :w91 answer lowers PB2
 * again. The original handles that answer at the earliest one loop pass later, after its 13.3 ms
 * key-scan delay, so a second generator still answering a :w92 of the other link (two sessions
 * opened close together) never reaches the PC. nx answers within 1-2 ms: without this hold, that
 * other answer would reach the PC and put Spooky2's replies one line out of step. So a pending
 * :w91 waits NX_W91_HOLD_MS after the last :w92 sequence of either link. */
static uint32_t s_w92_done;   /* nx_ms() when the last :w92 sequence ended */
static uint8_t s_w92_seen;    /* s_w92_done is valid */
static uint8_t s_held[2];     /* the pending :w91 of that link was held (counted once) */

static int tag_is(unsigned ch, const char *t)
{
    char tag[4];
    link_frame_tag(ch, tag);
    return tag[0] == t[0] && tag[1] == t[1] && tag[2] == t[2];
}

static void link_task(void)
{
    for (unsigned ch = 0; ch < 2u; ch++) {   /* a link's line waits until its queue is idle */
        if (!(g_ui_flags & UIF_LINE(ch)) || !nx_txq_idle(ch))
            continue;
        /* the time now, not the pass's start: the other link's :w92 may have ended just before */
        if (s_w92_seen && nx_ms() - s_w92_done < NX_W91_HOLD_MS && tag_is(ch, "w91")) {
            if (!s_held[ch])
                g_nx_diag.w91_held++;
            s_held[ch] = 1;
            continue;
        }
        s_held[ch] = 0;
        int w92 = tag_is(ch, "w92");
        link_process(ch);
        if (w92) {
            s_w92_done = nx_ms();
            s_w92_seen = 1;
        }
    }
}

void nx_loop_reset(void)
{
    s_w92_done = 0;
    s_w92_seen = 0;
    s_held[0] = s_held[1] = 0;
    nx_timer_reset();
    nx_slots_reset();
}

void nx_loop_pass(void)
{
    uint32_t now = nx_ms();
    nx_wdg_feed();
    nx_diag_loop(now);
    nx_fault_task(now);
    if (nx_core_power_off_due())
        nx_core_power_off();   /* never returns: no link or runner work after the request */
    nx_core_update_ready(now);
    if (nx_core_ready()) {
        nx_core_pcmode_task();   /* may block for the shared PC-mode sequence (<= ~110 ms) */
        nx_runner_task();        /* runner planning precedes link work, as in the original */
        nx_txq_service();
        link_task();
        if ((g_factory_cmd_flags & 1u) && nx_txq_idle(0) && nx_txq_idle(1))
            link_factory_execute();
        nx_resume_task(now);   /* after the link work: its writes wait for quiet queues */
        nx_chain_task(now);   /* before the timers: the next program cancels "all finished" */
        nx_terrain_task(now);
        nx_manual_task(now);
        nx_preset_task(now);
        nx_timer_task(now);
        nx_hist_task(now);
        nx_slots_task();
    }
    nx_ui_tick(now);
    nx_ui_render_step(NX_UI_SLICE_US);
}

#ifndef HOST_TEST
#include "stm32g0xx.h"

static int work_pending(void)
{
    return (g_ui_flags & (UIF_LINE_G1 | UIF_LINE_G2)) || (g_factory_cmd_flags & 1u) || !nx_txq_can_sleep()
           || nx_keys_pending() || nx_ui_busy() || nx_core_runner_held();
}

void nx_run(void)
{
    for (;;) {
        nx_loop_pass();
        /* Sleep until the next interrupt (at the latest the next SysTick, 1 ms) unless work is
         * pending; interrupts stay masked between the check and WFI, so none is missed. */
        __disable_irq();
        if (!work_pending()) {
            __DSB();
            __WFI();
        }
        __enable_irq();
    }
}
#endif
