/* Session timers (nx_timer.h). */
#include "nx_timer.h"

#include "nx_config.h"
#include "nx_core.h"
#include "nx_hist.h"
#include "nx_settings.h"
#include "nx_ui.h"
#include "nx_vm.h"
#include "state.h"

#if NX_FEAT_TIMERS
static uint32_t s_run_ms[2];   /* running time since the last stop */
static uint32_t s_last_ms;
static uint8_t s_last_ok;      /* s_last_ms is valid */
static uint8_t s_was_run[2];
static uint8_t s_done_due;     /* a program ended: offer the power-off once nothing is left */
static uint16_t s_lim[2];      /* the limits the counts belong to */

void nx_timer_reset(void)
{
    s_run_ms[0] = s_run_ms[1] = 0;
    s_was_run[0] = s_was_run[1] = 0;
    s_last_ok = s_done_due = 0;
    s_lim[0] = g_nx_settings.session_limit_min[0];
    s_lim[1] = g_nx_settings.session_limit_min[1];
}

void nx_timer_finished(unsigned ch)
{
    (void)ch;
    s_done_due = 1;
}

void nx_timer_goes_on(void)
{
    s_done_due = 0;
}

static uint32_t limit_ms(unsigned ch)
{
    return (uint32_t)g_nx_settings.session_limit_min[ch] * 60000u;
}

uint32_t nx_timer_left_ms(unsigned ch)
{
    ch &= 1u;
    uint32_t lim = limit_ms(ch);
    if (!lim || !((g_ui_flags & UIF_RUN(ch)) || (g_ch[ch].prog_slot && g_ch[ch].elapsed > 1u)))
        return 0;   /* no limit, or stopped: nothing counts */
    return s_run_ms[ch] < lim ? lim - s_run_ms[ch] : 1u;
}

void nx_timer_task(uint32_t now_ms)
{
    uint32_t dt = s_last_ok ? now_ms - s_last_ms : 0u;
    s_last_ms = now_ms;
    s_last_ok = 1;
    if (g_ui_state == ST_PC || g_ui_state == ST_CAL || !nx_core_ready()) {
        s_run_ms[0] = s_run_ms[1] = 0;
        s_was_run[0] = s_was_run[1] = 0;
        s_done_due = 0;
        return;
    }
    int busy = 0;
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct channel *c = &g_ch[ch];
        int run = (g_ui_flags & UIF_RUN(ch)) != 0;
        if (g_nx_settings.session_limit_min[ch] != s_lim[ch]) {   /* set or changed: from now */
            s_lim[ch] = g_nx_settings.session_limit_min[ch];
            s_run_ms[ch] = 0;
        }
        if (run && !s_was_run[ch])
            s_done_due = 0;   /* a program (re)starts: the offer waits for its end */
        s_was_run[ch] = (uint8_t)run;
        if (!run) {
            if (!c->prog_slot || c->elapsed <= 1u)
                s_run_ms[ch] = 0;   /* stopped, finished or empty: the count starts again */
            else
                busy = 1;           /* paused */
            continue;
        }
        s_run_ms[ch] += dt;
        uint32_t lim = limit_ms(ch);
        if (lim && s_run_ms[ch] >= lim) {
            struct nx_action a = { NXA_STOP, (uint8_t)ch, 0, 0, 0 };
            nx_hist_end(ch, NX_HEND_TIMER);
            if (nx_core_act(&a) == NX_OK) {
                nx_ui_post(NXE_TIMER_STOP, (uint8_t)ch, g_nx_settings.session_limit_min[ch]);
                s_run_ms[ch] = 0;
                s_was_run[ch] = 0;
                s_done_due = 1;
                continue;
            }
        }
        busy = 1;
    }
    if (s_done_due && !busy) {
        s_done_due = 0;
        if (g_nx_settings.off_when_done)
            nx_ui_post(NXE_ALL_DONE, 0, 0);
    }
}
#else
void nx_timer_reset(void)
{
}

void nx_timer_goes_on(void)
{
}

void nx_timer_task(uint32_t now_ms)
{
    (void)now_ms;
}

void nx_timer_finished(unsigned ch)
{
    (void)ch;
}

uint32_t nx_timer_left_ms(unsigned ch)
{
    (void)ch;
    return 0;
}
#endif
