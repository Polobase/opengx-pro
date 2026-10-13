/* Gallery fixtures of the UI screens (docs/nx/design/ui.md §7): every Drop-1 screen and state
 * from the fixture core of nxg_core.c, driven like the firmware drives the UI (keys through the
 * gesture engine every 10 ms, nx_ui_tick every millisecond) on the gallery's virtual clock.
 * Key screens also in the light and high-contrast themes; frames of the push, the sheet reveal,
 * the toast slide, the digit roll and the list highlight. Shot names: <screen>[_<variant>][_<theme>]. */
#include <string.h>

#include "nx_config.h"
#include "nx_keys.h"
#include "nx_lcd.h"
#include "nx_scope.h"
#include "nx_trans.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nxg_core.h"
#include "nxg_sheets.h"

static uint8_t s_raw;   /* the keys held down */

/* The main loop on the virtual clock: the UI every millisecond, keys and a complete render
 * every 10 ms (transitions are due by time, so the pictures do not depend on the render rate;
 * rendering less often keeps the emulator run short). */
static void run(uint32_t ms)
{
    for (uint32_t end = g_nxg_ms + ms; g_nxg_ms < end;) {
        g_nxg_ms++;
        nx_ui_tick(g_nxg_ms);
        if (g_nxg_ms % 10u == 0) {
            nx_keys_feed(s_raw, g_nxg_ms);
            nxg_settle();
        }
    }
    nxg_settle();
}

static void press(uint8_t keys, uint32_t hold_ms)
{
    s_raw = keys;
    run(hold_ms);
    s_raw = 0;
    run(60);
}

static const char *const k_theme_sfx[3] = { "", "_light", "_hc" };

static void shot_t(const char *base, uint8_t theme)
{
    char name[40];
    strcpy(name, base);
    strcat(name, k_theme_sfx[theme]);
    nxg_take(name);
}

/* A fresh UI on the default fixture; `patch` may change the fixture before the UI starts. */
static void begin(uint8_t theme, void (*patch)(void))
{
    nxg_fix_defaults();
    struct nx_uiset u;
    nx_uiset_from_blob(&u, g_nxg.blob);
    u.theme = theme == NX_THEME_NIGHT ? NX_THEME_DARK : theme;
    u.night = theme == NX_THEME_NIGHT;
    nx_uiset_to_blob(&u, g_nxg.blob);
    if (patch)
        patch();
    s_raw = 0;
    nx_keys_reset();
    /* each fixture starts at a time the scope's phase clock treats alike (a whole number of its 2 s
     * cycles and of its NX_SCOPE_FRAME_MS frames), so a shot does not depend on the fixtures before
     * it; the virtual clock jumps there before nx_ui_init, so no UI timer sees the jump */
    g_nxg_ms = (g_nxg_ms / (2000u * NX_SCOPE_FRAME_MS) + 1u) * (2000u * NX_SCOPE_FRAME_MS);
    nx_ui_init();
    run(300);   /* the name marquees are still in their 1 s pause */
}

/* ---- fixture patches ---- */

static void g1_none(void)
{
    memset(&g_nxg.vm.g[0], 0, sizeof g_nxg.vm.g[0]);
}

static void g1_ready(void)
{
    struct nx_gen_vm *g = &g_nxg.vm.g[0];
    g->state = NXG_READY;
    g->step = 1;
    g->left_s = g->total_s;
    g->step_left_s = g->dwell_s;
    g->frac = 0;
    g->freq = 4328u;
    g->next = 7278u;
}

static void g1_paused(void)
{
    g_nxg.vm.g[0].state = NXG_PAUSED;
}

static void g1_loop(void)
{
    g_nxg.vm.g[0].loop = 1;
}

static void g1_error(void)
{
    g_nxg.vm.g[0].link_down = 1;
}

static void g1_unknown_wave(void)
{
    g_nxg.vm.g[0].wave = 41;
}

static void g2_none(void)
{
    memset(&g_nxg.vm.g[1], 0, sizeof g_nxg.vm.g[1]);
}

static void resume_offer(void)   /* after a power cut: nothing loaded yet, both offered */
{
    memset(&g_nxg.vm.g, 0, sizeof g_nxg.vm.g);
    g_nxg.resume_offer = 3;
}

#if NX_FEAT_TIMERS
static void timer_on(void)   /* G1's "stop after": 27 minutes left */
{
    g_nxg.vm.g[0].timer_min = 27;
}

static void auto_offer(void)   /* power-on: the last programs offered */
{
    resume_offer();
    g_nxg.resume_auto = 1;
}

static void all_done(void)   /* G1 has finished, G2 is empty: "power off when all finished" */
{
    memset(&g_nxg.vm.g[1], 0, sizeof g_nxg.vm.g[1]);
    g_nxg.vm.g[0].state = NXG_READY;
    g_nxg.vm.g[0].stopped = 1;
}
#endif

#if NX_FEAT_CHAINS
static unsigned builtin_rows(void)   /* the rows of menu > Chains before "Chain 1" */
{
    unsigned n = 0;
#if NX_FEAT_TERRAIN
    n += nx_uc_terrain_count();
#endif
#if NX_FEAT_PRESET
    n += nx_uc_preset_count();
#endif
    return n;
}

static void chain_on(void)   /* G1 plays chain 1 (Lyme, Detox, Immune): the second of three */
{
    g_nxg.vm.g[0].chain = 1;
    g_nxg.vm.g[0].chain_item = 2;
    g_nxg.vm.g[0].chain_n = 3;
}
#endif
#if NX_FEAT_PRESET
static void preset_on(void)   /* G2 plays the built-in preset in a sweep: step 4 of 18, 80.41 Hz, 396 Hz next */
{
    struct nx_gen_vm *g = &g_nxg.vm.g[1];
    g->state = NXG_RUNNING;
    g->slot = NX_UC_PRESET;
    g->loop = 1;
    g->wave = 11;
    g->ampl_cv = 1000;
    g->step = 4;
    g->count = 18;
    g->dwell_s = 11;
    g->total_s = 198;
    g->left_s = 158;
    g->step_left_s = 4;
    g->frac = 13200;
    g->freq = 80416u;
    g->next = 396006u;
}
#endif
#if NX_FEAT_TERRAIN
static void terrain_on(void)   /* G1 plays Terrain · Remote (protocol 1), day 6: step 12 of the set's 32 */
{
    struct nx_gen_vm *g = &g_nxg.vm.g[0];
    g->terrain = 1;
    g->t_day = 6;
    g->t_days = 11;
    g->slot = NXG_SLOT_TSET;   /* as the table has the set: 32 steps of 78 s, square, 5 V */
    g->wave = 12;
    g->ampl_cv = 500;
    g->step = 12;
    g->count = 32;
    g->dwell_s = 78;
    g->total_s = 32u * 78u;
    g->left_s = g->total_s - 11u * 78u - 30u;
    g->step_left_s = 48;
    g->frac = (uint16_t)((uint32_t)(g->total_s - g->left_s) * 65535u / g->total_s);
    g->freq = 1658u;           /* 165 Hz */
    g->next = 2008u;
}
#endif

static void guide_unseen(void)   /* a first boot: the key guide after the splash */
{
    struct nx_uiset u;
    nx_uiset_from_blob(&u, g_nxg.blob);
    u.guide = 0;
    nx_uiset_to_blob(&u, g_nxg.blob);
}

static void not_ready(void)   /* the first ~4.4 s: STARTING in the status bar */
{
    g_nxg.vm.s.ready = 0;
}

static void pc_mode(void)
{
    g_nxg.vm.s.pc = 1;
}

static void pc_sync(void)
{
    g_nxg.vm.s.pc = 1;
    g_nxg.vm.s.pc_sync = 1;
    g_nxg.vm.s.pc_out = 0x05u;
}

/* What the PC set: G1 a sine on both outputs, 20 V, gated; G2 a square at 12 V on Out1 and an
 * inverse sawtooth at 5 V on Out2. */
static void pc_info(void)
{
    struct nx_sys_vm *s = &g_nxg.vm.s;
    s->pc = 1;
    s->pc_out = 0x0Fu;
    s->pc_seen[0] = s->pc_seen[1] = NX_PCS_WAVE1 | NX_PCS_AMPL1 | NX_PCS_WAVE2 | NX_PCS_AMPL2;
    s->pc_seen[0] |= NX_PCS_GATE;
    s->pc_gate = 1;
    s->pc_wave[0][0] = s->pc_wave[0][1] = 11;
    s->pc_ampl[0][0] = s->pc_ampl[0][1] = 2000;
    s->pc_wave[1][0] = 12;
    s->pc_wave[1][1] = 14;
    s->pc_ampl[1][0] = 1200;
    s->pc_ampl[1][1] = 500;
}

/* Wave tables of several cycles (Spooky2's Wave Cycle Multiplier): the PC sends the frequency
 * divided by their number, the lines show the output frequency and the cycles. G1 as Spooky2's
 * Radionics presets set it: 11 sine cycles at 37.9 Hz, Out2 the inverse and following, 10 V
 * (what Spooky2 sends for the preset's 20 V at a 100 % offset).
 * G2: a long number in the small font on an H-Bomb table of 16 cycles, and an Out2 of its own. */
static void pc_wcm(void)
{
    struct nx_sys_vm *s = &g_nxg.vm.s;
    s->pc = 1;
    s->pc_out = 0x0Fu;
    s->pc_sync = 1;
    s->pc_seen[0] = NX_PCS_WAVE1 | NX_PCS_AMPL1 | NX_PCS_WAVE2 | NX_PCS_AMPL2;
    s->pc_wave[0][0] = 11;
    s->pc_wave[0][1] = 25;
    s->pc_ampl[0][0] = s->pc_ampl[0][1] = 1000;
    s->pc_seen[1] = NX_PCS_WAVE1 | NX_PCS_WAVE2;
    s->pc_wave[1][0] = 18;
    s->pc_wave[1][1] = 11;
    s->pc_freq[0][0] = 3445454550u;      /* 3.44545455 Hz */
    s->pc_freq[1][0] = 581915945490u;    /* 581.91594549 Hz */
    s->pc_freq[1][1] = 368u;             /* 36 Hz */
    s->pc_mult[0][0] = s->pc_mult[0][1] = s->pc_mult[1][1] = 11;
    s->pc_mult[1][0] = 16;
}

static void cal_mode(void)
{
    g_nxg.vm.s.cal = 1;
}

static void splash_on(void)
{
    struct nx_uiset u;
    nx_uiset_from_blob(&u, g_nxg.blob);
    u.splash = NX_SPLASH_FULL;
    nx_uiset_to_blob(&u, g_nxg.blob);
}

/* ---- fixtures ---- */

static void dashboards(void)
{
    static const struct {
        const char *name;
        void (*patch)(void);
        uint8_t themes;   /* bit 0 dark, 1 light, 2 high contrast */
    } k[] = {
        { "dash_running", 0, 7 },         { "dash_idle", g1_none, 7 },      { "dash_ready", g1_ready, 1 },
        { "dash_paused", g1_paused, 1 },  { "dash_loop", g1_loop, 1 },      { "dash_error", g1_error, 3 },
        { "dash_wave_unknown", g1_unknown_wave, 1 },                          { "dash_strip_none", g2_none, 1 },
        { "dash_starting", not_ready, 1 },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        for (uint8_t t = 0; t < 3u; t++)
            if ((k[i].themes >> t) & 1u) {
                begin(t, k[i].patch);
                shot_t(k[i].name, t);
            }
    /* finished: the runner's event sets the latch */
    begin(NX_THEME_DARK, g1_ready);
    nx_ui_post(NXE_FINISHED, 0, 0);
    run(300);
    nxg_take("dash_finished");
    /* loop passes */
    begin(NX_THEME_DARK, g1_loop);
    nx_ui_post(NXE_FINISHED, 0, 1);
    nx_ui_post(NXE_FINISHED, 0, 1);
    run(50);
    nxg_take("dash_loop_passes");
    /* G2 focused: pushed in from the right, the strip shows G1 */
    begin(NX_THEME_DARK, 0);
    press(NX_KEY_G2, 100);
    run(400);
    nxg_take("dash_g2");
}

static void sheets(void)
{
    for (uint8_t t = 0; t < 2u; t++) {
        begin(t, 0);
        press(NX_KEY_UP, 80);
        run(300);
        shot_t("quick", t);
    }
}

static void library(void)
{
    for (uint8_t t = 0; t < 3u; t++) {
        begin(t, 0);
        press(NX_KEY_G1, 80);   /* G1 is focused: its library */
        run(400);
        press(NX_KEY_DOWN, 80);
        run(300);
        shot_t("library", t);
    }
    begin(NX_THEME_DARK, 0);
    press(NX_KEY_G1, 80);
    run(400);
    press(NX_KEY_DOWN, 80);
    run(300);
    press(NX_KEY_LOOP, 700);   /* held: the detail of slot 2 */
    run(400);
    nxg_take("detail");
    press(NX_KEY_DOWN, 80);
    press(NX_KEY_DOWN, 80);
    run(300);
    nxg_take("detail_scrolled");
    /* LOOP held on the dashboard: the detail of G1's program, at its playing step 12 */
    begin(NX_THEME_DARK, 0);
    press(NX_KEY_LOOP, 700);
    run(400);
    nxg_take("detail_playing");
#if NX_FEAT_SLOTS
    press(NX_KEY_LOOP, 80);   /* the slot actions */
    run(400);
    nxg_take("slot_actions");
    press(NX_KEY_DOWN, 80);   /* Copy: the program list picks the target */
    press(NX_KEY_START, 80);
    run(400);
    nxg_take("slot_pick");
#endif
}

static void settings(void)
{
    for (uint8_t t = 0; t < 3u; t++) {   /* DOWN on the dashboard: the menu, on Settings */
        begin(t, 0);
        press(NX_KEY_DOWN, 80);
        run(400);
        shot_t("menu", t);
        press(NX_KEY_START, 80);
        run(400);
        shot_t("settings", t);
    }
    static const char *const k_page[4] = { "settings_display", "settings_keys", "settings_motion",
                                           "settings_about" };
    for (unsigned p = 1; p <= 4u; p++) {
        begin(NX_THEME_DARK, 0);
        nx_ui_push(&nx_scr_set, p, NX_TR_NONE);
        run(300);
        nxg_take(k_page[p - 1u]);
    }
#if NX_FEAT_SUPPORT
    begin(NX_THEME_DARK, 0);   /* Settings > About > Support the project */
    nx_ui_push(&nx_scr_set, 4, NX_TR_NONE);
    nx_ui_push(&nx_scr_support, 0, NX_TR_NONE);
    run(300);
    nxg_take("support");
#endif
#if NX_FEAT_TIMERS
    begin(NX_THEME_DARK, 0);
    nx_uc_opt_set(NX_OPT_STOP_G1, 30);
    nx_uc_opt_set(NX_OPT_OFF_DONE, 1);
    nx_ui_push(&nx_scr_set, NX_SET_TIMERS, NX_TR_NONE);
    run(300);
    nxg_take("settings_timers");
    nx_uc_opt_set(NX_OPT_STOP_G1, 0);
    nx_uc_opt_set(NX_OPT_OFF_DONE, 0);
#endif
#if NX_FEAT_CHAINS
    begin(NX_THEME_DARK, chain_on);
    nx_ui_push(&nx_scr_chains, 0, NX_TR_NONE);
    run(300);
    for (unsigned i = 0; i < builtin_rows() + NX_CHAINS - 1u; i++)
        press(NX_KEY_DOWN, 80);   /* to "Chain 4": the chains come into view below the built-in rows */
    for (unsigned i = 0; i < NX_CHAINS - 1u; i++)
        press(NX_KEY_UP, 80);     /* back to "Chain 1" */
    run(400);
    nxg_take("chains");
    nx_ui_push(&nx_scr_chain_edit, 1, NX_TR_NONE);
    run(300);
    press(NX_KEY_DOWN, 80);
    run(300);
    nxg_take("chain_edit");
    begin(NX_THEME_DARK, chain_on);
    nxg_take("dash_chain");
#if NX_FEAT_PLANS
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_plan_edit, 0x101u, NX_TR_NONE);   /* plan 1 */
    run(300);
    nxg_take("plan_edit");
#endif
#if NX_FEAT_MANUAL
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_manual, 0x100u, NX_TR_NONE);
    run(300);
    press(NX_KEY_DOWN, 80);
    press(NX_KEY_DOWN, 80);   /* Start: its 1 Hz digit */
    run(400);
    nxg_take("manual_sweep");
    press(NX_KEY_UP, 80);
    press(NX_KEY_G1, 80);     /* Mode: Fixed */
    press(NX_KEY_DOWN, 80);
    press(NX_KEY_LOOP, 80);   /* the 10 Hz digit */
    run(400);
    nxg_take("manual_fixed");
    press(NX_KEY_UP, 80);
    press(NX_KEY_G2, 80);     /* back to Sweep for the next shots */
#endif
#if NX_FEAT_PRESET
    begin(NX_THEME_DARK, preset_on);
    nx_ui_set_focus(1);
    nx_ui_push(&nx_scr_chains, 0, NX_TR_NONE);
    run(300);
    for (unsigned i = 0; i < nx_uc_terrain_count(); i++)
        press(NX_KEY_DOWN, 80);   /* the preset's row, after the Terrain protocols */
    run(400);
    nxg_take("chains_preset");
    begin(NX_THEME_DARK, preset_on);
    press(NX_KEY_G2, 80);         /* G2's dashboard */
    run(700);
    nxg_take("dash_preset");
#endif
#if NX_FEAT_TERRAIN
    begin(NX_THEME_DARK, terrain_on);
    nx_ui_push(&nx_scr_chains, 0, NX_TR_NONE);   /* the list opens on the first Terrain row */
    run(400);
    nxg_take("chains_terrain");
    nx_ui_push(&nx_scr_terrain, 1, NX_TR_NONE);
    run(300);
    nxg_take("terrain_remote");
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_terrain, 2 | 1u << 8, NX_TR_NONE);
    run(300);
    nxg_take("terrain_contact");
    begin(NX_THEME_DARK, terrain_on);
    nxg_take("dash_terrain");
    nx_ui_post(NXE_ALERT, 0, 3);
    run(700);
    nxg_take("terrain_alert");
#endif
#endif
#if NX_FEAT_HIST
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_hist, 0, NX_TR_NONE);
    run(300);
    nxg_take("history");
#endif
#if NX_FEAT_DIAG
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_diag, 0, NX_TR_NONE);
    run(300);
    nxg_take("diagnostics");
    nx_ui_push(&nx_scr_keytest, 0, NX_TR_NONE);
    run(200);
    press(NX_KEY_UP, 80);
    run(100);
    s_raw = NX_KEY_START;   /* START down */
    run(200);
    nxg_take("key_test");
    s_raw = 0;
    run(100);
#endif
}

static void system_screens(void)
{
    for (uint8_t t = 0; t < 3u; t++) {
        begin(t, pc_mode);
        shot_t("pc_monitor", t);
    }
    begin(NX_THEME_DARK, pc_sync);
    nxg_take("pc_monitor_sync");
    begin(NX_THEME_DARK, pc_info);
    nxg_take("pc_monitor_info");
    begin(NX_THEME_DARK, pc_wcm);
    nxg_take("pc_monitor_wcm");
    for (uint8_t t = 0; t < 3u; t++) {
        begin(t, cal_mode);
        nx_ui_reset(&nx_scr_cal, 0, NX_TR_NONE);
        nx_ui_post(NXE_CAL_VALUE, 0, 21);
        nx_ui_post(NXE_CAL_VALUE, 0, 22);
        run(100);
        shot_t("calibration", t);
    }
    /* splash frames (full: 1.2 s) */
    begin(NX_THEME_DARK, splash_on);   /* begin() ran 400 ms of it */
    nxg_take("splash_400");
    run(300);
    nxg_take("splash_700");
    run(400);
    nxg_take("splash_1100");
    begin(NX_THEME_DARK, guide_unseen);   /* the first-boot key guide */
    nxg_take("key_guide");
    /* goodbye (its ring closes over 600 ms) */
    begin(NX_THEME_DARK, 0);
    nx_ui_goodbye();
    for (unsigned i = 0; i < 300u; i++) {
        g_nxg_ms++;
        nx_ui_goodbye_step(1200u - i);
        nxg_settle();
    }
    nxg_take("goodbye");
    /* the night view: night palette, after the dim and off times */
    begin(NX_THEME_DARK, 0);
    g_uiset.dim = 1;       /* 30 s */
    g_uiset.off = 1;       /* then 1 min */
    g_uiset.idle = NX_IDLE_AMBIENT;
    g_nxg_ms += 31000u;    /* no key for 31 s: dimmed */
    run(20);
    g_nxg_ms += 61000u;    /* and a minute more: the night view */
    run(100);
    nxg_take("night_view");
    /* the reset confirmation, START held half of its second */
    begin(NX_THEME_DARK, 0);
    nx_ui_push(&nx_scr_set, 0, NX_TR_NONE);
    run(200);
    for (unsigned i = 0; i < 4u; i++)
        press(NX_KEY_DOWN, 40);
    press(NX_KEY_START, 60);
    run(400);
    s_raw = NX_KEY_START;
    run(500);
    nxg_take("confirm_reset");
    s_raw = 0;
    run(100);
}

static void overlays(void)
{
    begin(NX_THEME_DARK, 0);
    nx_ui_post(NXE_CAL_VALUE, 0, 21);   /* the factory port outside calibration: "Factory value" */
    run(300);
    nxg_take("toast");
    begin(NX_THEME_DARK, 0);
    press(NX_KEY_G1 | NX_KEY_G2, 1700);   /* lock */
    g_nxg_ms += 4000u;                    /* the "Keys locked" toast is over */
    run(300);
    press(NX_KEY_START, 80);
    run(200);
    nxg_take("lock_card");
    s_raw = NX_KEY_START | NX_KEY_STOP;
    run(800);
    nxg_take("lock_unlocking");
    run(900);
    s_raw = 0;
    run(300);
    begin(NX_THEME_DARK, 0);
    s_raw = NX_KEY_POWER;
    run(520);
    nxg_take("power_ring");
    s_raw = 0;
    run(300);
    begin(NX_THEME_DARK, 0);
    s_raw = NX_KEY_STOP;
    run(800);
    nxg_take("stop_both_ring");
    s_raw = 0;
    run(300);
    begin(NX_THEME_DARK, resume_offer);   /* the resume card, 5 s into its 15 s ring */
    run(5000);
    nxg_take("resume_offer");
    g_nxg.resume_offer = 0;
    run(300);
#if NX_FEAT_TIMERS
    begin(NX_THEME_DARK, auto_offer);   /* the auto-start card at power-on, 3 s into its 10 s */
    run(3000);
    nxg_take("auto_offer");
    g_nxg.resume_offer = 0;
    g_nxg.resume_auto = 0;
    run(300);
    begin(NX_THEME_DARK, timer_on);
    nxg_take("dash_timer");
    begin(NX_THEME_DARK, all_done);   /* the power-off card, 5 s into its 15 s ring */
    nx_ui_post(NXE_ALL_DONE, 0, 0);
    run(5000);
    nxg_take("done_card");
    press(NX_KEY_STOP, 80);   /* cancelled */
    run(300);
#endif
}

static void frames(void)
{
    /* G1 -> G2 push */
    begin(NX_THEME_DARK, 0);
    s_raw = NX_KEY_G2;
    uint32_t t0 = g_nxg_ms;
    while (!nx_trans_active() && g_nxg_ms - t0 < 200u)
        run(1);
    run(60);
    nxg_take("frame_push_060");
    run(60);
    nxg_take("frame_push_120");
    s_raw = 0;
    run(400);
    /* quick settings sheet reveal */
    begin(NX_THEME_DARK, 0);
    s_raw = NX_KEY_UP;
    t0 = g_nxg_ms;
    while (!nx_trans_active() && g_nxg_ms - t0 < 200u)
        run(1);
    /* the reveal starts its clock once the scrimmed base is out; with DMA and 2 ms slices that is one
     * settle later than on the host: settle once more at the same time, so both start it together */
    nxg_settle();
    run(100);
    nxg_take("frame_reveal_100");
    s_raw = 0;
    run(400);
    /* toast sliding in */
    begin(NX_THEME_DARK, 0);
    nx_ui_post(NXE_FLASH_ERROR, 0, 0);   /* "Settings could not be saved" */
    run(100);
    nxg_take("frame_toast_100");
    run(400);
    /* digit roll: the next step's frequency */
    begin(NX_THEME_DARK, 0);
    g_nxg.vm.g[0].freq = 7288u;
    g_nxg.vm.g[0].step = 13;
    run(80);
    nxg_take("frame_roll_080");
    run(400);
    nxg_take("frame_roll_end");
    /* library highlight sliding down */
    begin(NX_THEME_DARK, 0);
    press(NX_KEY_G1, 80);
    run(400);
    s_raw = NX_KEY_DOWN;
    run(80);
    nxg_take("frame_list_slide");
    s_raw = 0;
    run(300);
}

void nxg_screens_run(void)
{
    dashboards();
    sheets();
    library();
    settings();
    system_screens();
    overlays();
    frames();
}
