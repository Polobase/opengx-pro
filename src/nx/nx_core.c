/* nx core (nx_core.h). */
#include "nx_core.h"
#include "nx_hist.h"

#include <string.h>

#include "core_calmenu.h"
#include "core_pcmode.h"
#include "link.h"
#include "link_hw.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_hw.h"
#include "nx_resume.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_time.h"
#include "nx_txq.h"
#include "nx_ui.h"
#include "nx_wdg.h"
#include "state.h"
#include "storage.h"

static uint8_t s_ready;       /* boot gate passed */
static uint8_t s_pc_wait;     /* a PC-mode entry/exit waits for the queues */
static uint8_t s_power_req;   /* NXA_POWER_OFF */

void nx_core_reset(void)
{
    s_ready = s_pc_wait = s_power_req = 0;
}

int nx_core_act(const struct nx_action *a)
{
    if (a->type == NXA_POWER_OFF) {
        s_power_req = 1;
        return NX_OK;
    }
    if (a->type < NXA_LOAD || a->type > NXA_SEEK || a->ch > 1u)
        return NX_EINVAL;
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    if (!s_ready || g_ui_state == ST_CAL || s_pc_wait)
        return NX_BUSY;
    unsigned ch = a->ch;
    struct channel *c = &g_ch[ch];
    switch (a->type) {
    case NXA_LOAD:   /* app.c list_keys START; another source (nx_src.h) is loaded the same way */
        if (!nx_src_is_slot(a->id) && nx_src_fill(a->id, (uint16_t)a->v, c) != NX_OK)
            return NX_EINVAL;
        c->step_sent = 0xFF;
#if NX_FEAT_BFB
        if (g_ch[ch ^ 1u].prog_slot != NX_SRC_BFB)   /* the other link resends its step too (the original), unless a
                                                      * scan drives it: its sweep's lines are its own */
#endif
            g_ch[ch ^ 1u].step_sent = 0xFF;
        flags_clear(&g_ui_flags, UIF_RUN(ch));
        c->elapsed = 0;
        if (nx_src_is_slot(a->id))
            program_load(a->id, ch);
        c->prog_slot = (uint8_t)a->id;
        nx_runner_loaded(ch);
        break;
    case NXA_PLAY:   /* app.c channel_keys START / LOOP on a stopped channel */
        c->step_sent = 0xFF;
        flags_set(&g_ui_flags, UIF_RUN(ch));
        if (a->v)
            flags_set(&g_ui_flags, UIF_LOOP(ch));
        else
            flags_clear(&g_ui_flags, UIF_LOOP(ch));
        break;
    case NXA_PAUSE:  /* START / LOOP while running */
        c->step_sent = 0xFF;
        flags_clear(&g_ui_flags, UIF_RUN(ch));
        if (a->v)
            flags_set(&g_ui_flags, UIF_LOOP(ch));
        else
            flags_clear(&g_ui_flags, UIF_LOOP(ch));
        break;
    case NXA_STOP:
        c->step_sent = 0xFF;
        c->elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        break;
    case NXA_STOP_ALL:   /* the original's short POWER press on a channel screen */
        g_ch[0].step_sent = g_ch[1].step_sent = 0xFF;
        g_ch[0].elapsed = g_ch[1].elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN_G1 | UIF_RUN_G2);
        break;
    case NXA_SEEK:   /* the runner sends the step at that time on its next pass */
        c->step_sent = 0xFF;
        c->elapsed = a->v;
        break;
    default:         /* NXA_LOOP */
        if (a->v)
            flags_set(&g_ui_flags, UIF_LOOP(ch));
        else
            flags_clear(&g_ui_flags, UIF_LOOP(ch));
        break;
    }
    return NX_OK;
}

void nx_core_update_ready(uint32_t now_ms)
{
    /* The original's splash loop ends after ~4 s or at the first factory-port command. */
    if (!s_ready && (now_ms >= NX_GEN_READY_MS || (g_factory_cmd_flags & 1u)))
        s_ready = 1;
}

int nx_core_ready(void)
{
    return s_ready;
}

void nx_core_pcmode_task(void)
{
    uint32_t pb1 = nx_pb1();
    if (pb1 ? g_ui_state != ST_PC : g_ui_state == ST_PC) {
        /* The original finishes a runner sequence before it looks at PB1: wait for both queues,
         * and plan nothing new meanwhile. */
        s_pc_wait = 1;
        if (!nx_txq_idle(0) || !nx_txq_idle(1))
            return;
        s_pc_wait = 0;
        if (pb1) {
            g_nx_diag.pc_entries++;
            memset(&g_nx_pcmon, 0, sizeof g_nx_pcmon);   /* this session's values only */
            nx_resume_pc(1);   /* what plays now, before the switch unloads it */
        }
        else
            g_nx_diag.pc_exits++;
        core_pcmode_update(pb1);
        if (!pb1)
            nx_resume_pc(0);   /* left: what played before is offered again */
    } else {
        s_pc_wait = 0;   /* PB1 went back before the queues were idle: the hold ends (it used to stay set
                          * until power-off: the runner held, every action busy) */
    }
    if (g_ui_state == ST_BOOT)
        g_ui_state = ST_HOME;
}

int nx_core_runner_held(void)
{
    return s_pc_wait;
}

int nx_core_boot(void)
{
    if ((nx_hw_keys() & (NX_KEY_G1 | NX_KEY_G2)) != (NX_KEY_G1 | NX_KEY_G2))
        return 0;
    /* app.c: calibration menu. Its key scan has no debounce here either: wait for the release,
     * ask G1 for its calibration values, wait up to 110 ms for the reply, then PB2 high and the
     * test signal of index 0. */
    g_ui_state = ST_CAL;
    s_ready = 1;
    nx_ui_cal_screen();
    uint32_t t0 = nx_ms();
    while (nx_hw_keys() && nx_ms() - t0 < NX_CAL_RELEASE_MS)   /* bounded: a stuck key */
        nx_wdg_feed();
    link_send_line(0, ":r80=0,");
    uint32_t sent = nx_ms();
    while (!(g_ui_flags & UIF_LINE_G1) && nx_ms() - sent < NX_CAL_R80_WAIT_MS)
        nx_wdg_feed();
    if (g_ui_flags & UIF_LINE_G1)
        link_process(0);
    ui_cal_draw_values();
    pin_high(GPIOB, HANDSHAKE_PIN);
    cal_apply_test();
    nx_keys_flush();   /* the boot chord's events are not menu keys */
    return 1;
}

static enum key_code key_code_of(uint8_t k)
{
    switch (k) {
    case NX_KEY_LOOP:
        return KEY_LOOP;
    case NX_KEY_START:
        return KEY_START;
    case NX_KEY_STOP:
        return KEY_STOP;
    case NX_KEY_UP:
        return KEY_UP;
    case NX_KEY_DOWN:
        return KEY_DOWN;
    case NX_KEY_G1:
        return KEY_G1;
    case NX_KEY_G2:
        return KEY_G2;
    case NX_KEY_POWER:
        return KEY_POWER;
    default:
        return KEY_NONE;
    }
}

int nx_core_cal_key(const struct nx_kev *e)
{
    if (g_ui_state != ST_CAL || (e->type != NXK_PRESS && e->type != NXK_REPEAT))
        return 0;
    enum key_code k = key_code_of(e->keys);
    if (k == KEY_NONE)
        return 0;
    return core_calmenu_key(k, e->type == NXK_REPEAT);
}

int nx_core_power_off_due(void)
{
    return s_power_req || nx_keys_power_off_due();
}

void nx_core_power_off(void)
{
    nx_resume_power_off();   /* switched off on purpose: nothing to resume */
    /* As the original, whose POWER press stops both generators before the hold switches off:
     * both get their idle configuration (outputs off) first, within 300 ms. Not in PC mode (the
     * PC owns the generators) nor in calibration. */
    if (s_ready && g_ui_state != ST_PC && g_ui_state != ST_CAL && !s_pc_wait
        && (g_ch[0].prog_slot || g_ch[1].prog_slot)) {
        struct nx_action a = { NXA_STOP_ALL, 0, 0, 0, 0 };
        nx_core_act(&a);
        for (uint32_t t0 = nx_ms(); nx_ms() - t0 < NX_OFF_STOP_MS;) {
            nx_wdg_feed();
            nx_runner_task();
            nx_txq_service();
            if (nx_txq_idle(0) && nx_txq_idle(1) && g_ch[0].step_sent != 0xFFu && g_ch[1].step_sent != 0xFFu)
                break;
        }
    }
    nx_hist_power_off();   /* the runs just ended and the waiting history records */
    uint32_t t0 = nx_ms();
    nx_ui_goodbye();
    for (uint32_t t = 0; t < NX_GOODBYE_MS; t = nx_ms() - t0) {
        nx_wdg_feed();
        nx_ui_goodbye_step(NX_GOODBYE_MS - t);
    }
    power_off_signal();
}
