/* Resume after a power cut (src/nx/nx_resume.h) on the host board model: the snapshot in the KV
 * store while a program plays, the offer after a power cut (the SPI flash kept, everything else
 * reset), START / STOP / the countdown on the offer's card, and what deletes the snapshot or
 * suppresses the offer: a clean power-off, both generators stopped, a slot changed by the PC,
 * the setting, PC mode. And the offer at the end of PC mode: what played when it began. */
#include "harness.h"

#include "link_internal.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_diag.h"
#include "nx_fault.h"
#include "nx_keys.h"
#include "nx_kv.h"
#include "nx_resume.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_time.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

static uint8_t s_flash[FLASH_SIZE];

/* 3 steps of 30 s, each its own frequency */
static void store_program(unsigned slot, uint64_t f0)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 11;
    p.hdr[HDR_AMPL] = 1000;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = 30;
    p.hdr[HDR_COUNT] = 3;
    for (unsigned i = 0; i < 3u; i++)
        p.freq[i] = f0 + 10u * i;
    uint16_t params[2 * PROGRAM_STEPS];
    memset(params, 0xFF, sizeof params);   /* no gating pairs: the runner zeroes them in RAM */
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
}

/* Power-on as main_nx.c: the SPI flash of the last run when kept, the reset reason, keys held
 * and PB1 at the start, the settings, the offer. */
static void power_on_as(int keep_flash, uint8_t reason, uint8_t keys, uint32_t pb1)
{
    struct nx_uiset u;   /* not a first boot: no key guide over the dashboard */
    nx_uiset_defaults(&u);
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    if (keep_flash)
        memcpy(s_flash, flash_mem, FLASH_SIZE);
    nxw_reset();
    if (keep_flash)
        memcpy(flash_mem, s_flash, FLASH_SIZE);
    g_nx_diag.reset_reason = reason;
    nxw_keys = keys;
    nxw_pb1 = pb1;
    nx_time_reset();   /* PB1 as it is at power-on */
    nx_store_mount();
    nx_settings_load(&g_nx_settings);
    nx_resume_boot();
}

static void power_on(int keep_flash)
{
    power_on_as(keep_flash, NX_RST_UNKNOWN, 0, 0);
}

static int act(uint8_t type, unsigned ch, unsigned id, uint32_t v)
{
    struct nx_action a = { type, (uint8_t)ch, (uint16_t)id, v, 0 };
    return nx_core_act(&a);
}

static int stored(struct nx_resume_rec *r)
{
    return nx_kv_get(NX_KEY_RESUME, r, sizeof *r) == (int)sizeof *r;
}

#if NX_FEAT_TIMERS
/* Auto-start at power-on: the programs last started (slot, loop flag) are kept while the setting
 * is on; a power-on without anything to resume offers them: the 10 s card starts them from the
 * start, START at once, STOP cancels. Not after a watchdog reset, not with a key held (POWER
 * aside), not in PC mode, not when there is something to resume. */
static void test_autostart(void)
{
    struct nx_last_rec l;
    power_on(0);
    store_program(4, 10000007u);
    store_program(5, 20000007u);
    g_nx_settings.autostart = 1;
    nxw_run_until(4600);
    CHECK_EQ(act(NXA_LOAD, 0, 5, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);   /* looped */
    nxw_run_ms(3000);
    CHECK(nx_kv_get(NX_KEY_LAST, &l, sizeof l) == (int)sizeof l);
    CHECK(l.slot[0] == 5 && l.slot[1] == 0 && l.loop == 1);
    nx_settings_save(&g_nx_settings);
    nx_resume_power_off();   /* a clean power-off: nothing to resume */

    power_on_as(1, NX_RST_POWER, NX_KEY_POWER, 0);   /* POWER still down from switching on */
    nxw_keys = 0;
    CHECK_EQ(nx_resume_offer(), 1);
    CHECK(nx_resume_auto());
    nxw_run_until(4600 + 9000);
    CHECK_EQ(g_ch[0].prog_slot, 0);   /* the card counts down */
    nxw_run_until(4600 + 11500);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK(g_ch[0].prog_slot == 5 && (g_ui_flags & UIF_RUN_G1) && (g_ui_flags & UIF_LOOP_G1));
    CHECK(g_ch[0].elapsed < 3000u);   /* from the start */
    CHECK_EQ(g_ch[1].prog_slot, 0);

    /* START at once */
    nx_resume_power_off();
    power_on_as(1, NX_RST_PIN, 0, 0);
    nxw_run_until(5000);
    nxw_press(NX_KEY_START, 150);
    CHECK(g_ch[0].prog_slot == 5 && (g_ui_flags & UIF_RUN_G1));

    /* STOP cancels */
    nx_resume_power_off();
    power_on_as(1, NX_RST_POWER, 0, 0);
    nxw_run_until(5000);
    nxw_press(NX_KEY_STOP, 150);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_run_ms(12000);
    CHECK_EQ(g_ch[0].prog_slot, 0);

    /* no offer: watchdog reset, a key held, PB1 high, the setting off */
    power_on_as(1, NX_RST_WATCHDOG, 0, 0);
    CHECK_EQ(nx_resume_offer(), 0);
    power_on_as(1, NX_RST_POWER, NX_KEY_START, 0);
    CHECK_EQ(nx_resume_offer(), 0);
    power_on_as(1, NX_RST_POWER, 0, 1);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_pb1 = 0;
    g_nx_settings.autostart = 0;
    nx_settings_save(&g_nx_settings);
    power_on_as(1, NX_RST_POWER, 0, 0);
    CHECK_EQ(nx_resume_offer(), 0);

    /* something to resume wins: the resume card, not the auto-start */
    g_nx_settings.autostart = 1;
    nx_settings_save(&g_nx_settings);
    power_on_as(1, NX_RST_POWER, 0, 0);
    nxw_run_until(4600);
    nxw_press(NX_KEY_STOP, 150);   /* the auto-start card: cancelled */
    CHECK_EQ(act(NXA_LOAD, 1, 4, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 1, 0, 0), NX_OK);
    nxw_run_ms(5000);
    power_on_as(1, NX_RST_POWER, 0, 0);   /* power cut */
    CHECK_EQ(nx_resume_offer(), 2);
    CHECK(!nx_resume_auto());
    nx_settings_defaults(&g_nx_settings);
}
#endif

void test_nx_resume(void)
{
    struct nx_resume_rec r;

    /* first boot: G1 plays slot 4, G2 is paused in slot 5 (looped) */
    power_on(0);
    store_program(4, 10000007u);
    store_program(5, 20000007u);
    nxw_run_until(4600);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK(!stored(&r));
    CHECK_EQ(act(NXA_LOAD, 0, 4, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 0), NX_OK);
    CHECK_EQ(act(NXA_LOAD, 1, 5, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 1, 0, 1), NX_OK);
    nxw_run_ms(5000);
    CHECK_EQ(act(NXA_PAUSE, 1, 0, 1), NX_OK);
    uint32_t g2_at = g_ch[1].elapsed;
    nxw_run_ms(3000);   /* the pause is written after the 2 s gap */
    CHECK(stored(&r));
    CHECK(r.g[0].slot == 4 && r.g[0].flags == NX_RES_RUN);
    CHECK(r.g[1].slot == 5 && r.g[1].flags == NX_RES_LOOP && r.g[1].elapsed_ms == g2_at);
    /* while it runs, the position is written every minute */
    nxw_run_ms(62000);
    CHECK(stored(&r));
    CHECK(r.g[0].elapsed_ms >= 60000u && r.g[0].elapsed_ms <= 70000u);
    uint32_t at = r.g[0].elapsed_ms;

    /* power cut, power on: the offer, shown after the boot gate; START resumes both */
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 3);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_run_until(5000);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(g_ch[0].prog_slot, 0);   /* nothing before the answer */
    nxw_press(NX_KEY_START, 150);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK(nx_ui_top() == &nx_scr_dash);   /* START answered the card: no program list */
    CHECK(g_ch[0].prog_slot == 4 && (g_ui_flags & UIF_RUN_G1) && !(g_ui_flags & UIF_LOOP_G1));
    CHECK(g_ch[0].elapsed >= at && g_ch[0].elapsed < at + 1000u);
    CHECK(g_ch[1].prog_slot == 5 && !(g_ui_flags & UIF_RUN_G2) && (g_ui_flags & UIF_LOOP_G2));
    CHECK_EQ(g_ch[1].elapsed, g2_at);
    nxw_run_ms(200);
    CHECK_EQ(g_ch[0].step, 3);   /* 60+ s in: the third step */
    CHECK_EQ(g_ch[0].step_sent, 3);
    CHECK_EQ(g_ch[0].step_freq, 10000027u);

    /* power cut again: nobody answers, the ring resumes after 15 s */
    nxw_run_ms(3000);
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 3);
    nxw_run_until(4600 + 14000);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_run_until(4600 + 16500);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK(g_ch[0].prog_slot == 4 && (g_ui_flags & UIF_RUN_G1));

    /* a clean power-off (POWER held) deletes it: no offer at the next power-on */
    nx_resume_power_off();
    CHECK(!stored(&r));
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 0);

    /* STOP on the card discards: nothing loaded, the snapshot deleted */
    nxw_run_until(4600);
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(stored(&r));
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 1);
    nxw_run_until(5000);
    nxw_press(NX_KEY_STOP, 150);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_run_ms(3000);
    CHECK(!stored(&r));

    /* both generators stopped: deleted */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(stored(&r));
    act(NXA_STOP, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(!stored(&r));

    /* the slot's program changed before the next boot (a PC upload): not offered */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(stored(&r));
    store_program(4, 30000007u);
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_run_ms(8000);
    CHECK(!stored(&r));   /* nothing plays: it goes */

    /* a program in RAM that is not the slot's own (uploaded by the PC into the channel) is not
     * kept */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    g_ch[0].prog.freq[1] = 12345u;
    nxw_run_ms(3000);
    CHECK(!stored(&r));
    act(NXA_STOP, 0, 0, 0);
    nxw_run_ms(100);

    /* PC mode: no write while the PC owns the generators; it unloads both (as the original), and
     * when it ends what played when it began is offered, at that position */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(stored(&r));
    uint32_t before = r.g[0].elapsed_ms;
    nxw_run_ms(20000);
    uint32_t pc_at = g_ch[0].elapsed;
    nxw_pb1 = 1;
    nxw_run_ms(65000);
    CHECK(stored(&r));
    CHECK_EQ(r.g[0].elapsed_ms, before);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK_EQ(nx_resume_offer(), 1);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(g_ch[0].prog_slot, 0);   /* nothing before the answer */
    nxw_run_ms(3000);
    CHECK(stored(&r));                /* kept while the card is open */
    nxw_press(NX_KEY_START, 150);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK(g_ch[0].prog_slot == 4 && (g_ui_flags & UIF_RUN_G1) && !(g_ui_flags & UIF_LOOP_G1));
    CHECK(g_ch[0].elapsed >= pc_at && g_ch[0].elapsed < pc_at + 1000u);

    /* the PC uploaded into the slot meanwhile: not offered, and the snapshot goes */
    nxw_pb1 = 1;
    nxw_run_ms(2000);
    store_program(4, 40000007u);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_run_ms(3000);
    CHECK(!stored(&r));

    /* STOP on the card discards */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    nxw_pb1 = 1;
    nxw_run_ms(2000);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK_EQ(nx_resume_offer(), 1);
    nxw_press(NX_KEY_STOP, 150);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_run_ms(3000);
    CHECK(!stored(&r));

    /* something loaded while the card is open: the offer goes, and what plays now is written at
     * once (not the position from before PC mode) */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    nxw_pb1 = 1;
    nxw_run_ms(2000);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK_EQ(nx_resume_offer(), 1);
    CHECK_EQ(act(NXA_LOAD, 1, 5, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 1, 0, 0), NX_OK);
    nxw_run_ms(100);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_run_ms(3000);
    CHECK(stored(&r) && r.g[0].slot == 0 && r.g[1].slot == 5 && r.g[1].flags == NX_RES_RUN);
    act(NXA_STOP, 1, 0, 0);
    nxw_run_ms(3000);
    CHECK(!stored(&r));

    /* a boot into PC mode drops the offer of a power cut */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 1);
    nxw_pb1 = 1;
    nxw_run_until(5000);
    CHECK_EQ(nx_resume_offer(), 0);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_pb1 = 0;

    /* the setting off: no offer, and the snapshot goes */
    nxw_run_ms(1000);
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(3000);
    CHECK(stored(&r));
    g_nx_settings.resume_mode = NX_RESUME_OFF;
    nx_settings_save(&g_nx_settings);
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 0);
    nxw_run_until(8000);
    CHECK(!stored(&r));
    act(NXA_LOAD, 0, 4, 0);   /* nor at the end of PC mode */
    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(1000);
    nxw_pb1 = 1;
    nxw_run_ms(2000);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK_EQ(nx_resume_offer(), 0);
    nx_settings_defaults(&g_nx_settings);
#if NX_FEAT_TIMERS
    test_autostart();
#endif
}
