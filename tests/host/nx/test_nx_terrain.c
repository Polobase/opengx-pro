/* Built-in Terrain sets as program sources (src/nx/nx_terrain.c, nx_src.c, the runner's dwell per
 * step) with the made-up tables of terrain_fixture.c. */
#include "harness.h"

#include "nx_config.h"
#include "nx_core.h"
#include "nx_kv.h"
#include "nx_resume.h"
#include "nx_runner.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_src.h"
#include "nx_terrain.h"
#include "nx_txq.h"
#include "nx_keys.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_uicore.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

static void run_ms(uint32_t ms)   /* runner and queues only, 50 us passes */
{
    for (uint32_t i = 0; i < ms * 20u; i++) {
        nx_runner_task();
        nx_txq_service();
        nxw_poll();
        sim_advance_us(50);
    }
}

static void sets(void)
{
    CHECK_EQ(nx_tset_steps(0), 3);
    CHECK_EQ(nx_tset_steps(2), 7);   /* 3 x 2 and the end step */
    CHECK_EQ(nx_tset_steps(3), 7);
    CHECK_EQ(nx_tset_steps(4), 0);
    CHECK_EQ(nx_tset_dwell_s(0, 1), 5);
    CHECK_EQ(nx_tset_dwell_s(3, 2), 3);   /* the second pass of B starts again at 3 s */
    CHECK_EQ(nx_tset_dwell_s(2, 6), 4);   /* the end step */
    CHECK(strcmp(nx_tset_name(1), "Demo kidney") == 0);
    CHECK(strcmp(nx_tset_name(9), "") == 0);
    struct program p;
    CHECK_EQ(nx_tset_fill(2, 0, &p), NX_OK);
    CHECK_EQ(p.hdr[HDR_WAVE], 12);
    CHECK_EQ(p.hdr[HDR_AMPL], 2000);
    CHECK_EQ(p.hdr[HDR_OFFS], 120);
    CHECK_EQ(p.hdr[HDR_COUNT], 7);
    CHECK_EQ(p.freq[0], 100006);    /* 100.00 Hz = 10000 x 10^-2 */
    CHECK_EQ(p.freq[2], 506);       /* 0.50 Hz */
    CHECK_EQ(p.freq[3], 100006);    /* the second pass */
    CHECK_EQ(p.freq[6], 0);         /* the end step: 0 Hz */
    CHECK_EQ(nx_tset_fill(1, 25, &p), NX_OK);
    CHECK_EQ(p.hdr[HDR_AMPL], 25);
    CHECK_EQ(p.freq[1], 21822156);  /* 21822.15 Hz */
    CHECK_EQ(nx_tset_fill(4, 0, &p), NX_EINVAL);
}

static void playing(void)
{
    nxw_reset();
    g_ui_state = ST_HOME;
    nx_core_update_ready(NX_GEN_READY_MS);
    struct nx_action load = { NXA_LOAD, 0, NX_SRC_SET + 0u, 0, 0 };
    CHECK_EQ(nx_core_act(&load), NX_OK);
    CHECK_EQ(g_ch[0].prog_slot, NX_SRC_SET);
    char name[NX_UC_NAME_MAX + 1u];
    CHECK_EQ(nx_uc_name(NX_SRC_SET, name), 12);
    CHECK(strcmp(name, "Demo cleanse") == 0);
    run_ms(60);   /* the idle configuration */
    nxw_clear();
    struct nx_action play = { NXA_PLAY, 0, 0, 0, 0 };
    CHECK_EQ(nx_core_act(&play), NX_OK);
    uint64_t t0 = sim_us;
    run_ms(4000);
    struct nx_run_times t;
    nx_runner_times(0, &t);   /* 4 s: step 2 of 3 s, 5 s, 3 s */
    CHECK_EQ(t.count, 3);
    CHECK_EQ(t.step, 2);
    CHECK_EQ(t.total_ms, 11000);
    CHECK_EQ(t.dwell_ms, 5000);
    CHECK(t.step_left_ms > 3990u && t.step_left_ms <= 4000u);
    run_ms(8000);
    char lines[64][32];
    unsigned n = nxw_lines(0, lines, 64), k = 0;
    uint64_t at[8];
    unsigned idx[8];
    for (unsigned i = 0; i < n && k < 8u; i++)
        if (strncmp(lines[i], ":w24=", 5) == 0) {
            idx[k] = i;
            at[k++] = nxw_line_us[0][i] - t0;
        }
    CHECK_EQ(k, 4);   /* three steps, then the idle configuration of step 1 */
    CHECK(at[0] < 1000u);
    CHECK(at[1] >= 3000000u && at[1] < 3001000u);
    CHECK(at[2] >= 8000000u && at[2] < 8001000u);
    CHECK(at[3] >= 11000000u && at[3] < 11010000u);
    CHECK(strcmp(lines[idx[1]], ":w24=250506,") == 0);
    CHECK(strcmp(lines[idx[2]], ":w24=506,") == 0);
    CHECK_EQ(g_ui_flags & UIF_RUN_G1, 0);   /* finished */

    /* a power level: the amplitude of the idle configuration */
    load.v = 25;
    CHECK_EQ(nx_core_act(&load), NX_OK);
    nxw_clear();
    run_ms(60);
    n = nxw_lines(0, lines, 64);
    k = 0;
    for (unsigned i = 0; i < n; i++)
        k += strcmp(lines[i], ":w28=25,") == 0 || strcmp(lines[i], ":w20=12,") == 0;
    CHECK_EQ(k, 2);

    load.id = NX_SRC_SET + 4u;   /* no such set */
    CHECK_EQ(nx_core_act(&load), NX_EINVAL);
    CHECK_EQ(g_ch[0].prog_slot, NX_SRC_SET);
}

#if NX_FEAT_TERRAIN
static uint8_t s_flash[FLASH_SIZE];

static void power_on(int keep_flash)
{
    if (keep_flash)
        memcpy(s_flash, flash_mem, FLASH_SIZE);
    nxw_reset();
    if (keep_flash)
        memcpy(flash_mem, s_flash, FLASH_SIZE);
    nx_store_mount();
    nx_settings_load(&g_nx_settings);
    nx_terrain_boot();
    nx_resume_boot();
}

static void run_s(unsigned s)
{
    for (unsigned i = 0; i < s * 1000u; i++)
        nxw_step(1000);
}

static void stop(unsigned ch)
{
    struct nx_action a = { NXA_STOP, (uint8_t)ch, 0, 0, 0 };
    nx_core_act(&a);
}

/* The protocols of the fixture: 1 = Remote (sets 0, 1, 0 for 1 h each), 2 = Contact (days of
 * sets 2+2, 2+3, 3+3; set 2 = 6 x 2 s + a 4 s end step). */
static void protocols(void)
{
    power_on(0);
    nxw_run_until(4600);
    CHECK_EQ(nx_terrain_count(), 2);
    CHECK_EQ(nx_terrain_items(1), 3);
    CHECK_EQ(nx_terrain_items(2), 6);
    CHECK_EQ(nx_terrain_set_of(2, 3), 3);
    CHECK_EQ(nx_terrain_play(0, 3), NX_EINVAL);

    /* Remote: looped; after its hours the next item, after the last the first again */
    CHECK_EQ(nx_terrain_play(0, 1), NX_OK);
    CHECK(g_ch[0].prog_slot == NX_SRC_SET && (g_ui_flags & UIF_RUN_G1) && (g_ui_flags & UIF_LOOP_G1));
    run_s(2);
    struct nx_trun t;
    nx_terrain_run(0, &t);
    CHECK(t.id == 1 && t.item == 0 && t.run_s >= 1u && t.run_s <= 2u);
    t.run_s = 3599;
    nx_terrain_resume(0, &t);
    nxw_clear();
    run_s(2);
    CHECK(g_ch[0].prog_slot == NX_SRC_SET + 1u && (g_ui_flags & UIF_LOOP_G1));
    {   /* the next set's own waveform goes out before its first step (loaded and played at once) */
        char lines[64][32];
        unsigned n = nxw_lines(0, lines, 64), w20 = 0, play = 0;
        for (unsigned i = 0; i < n; i++) {
            if (strcmp(lines[i], ":w20=14,") == 0)
                w20 = i + 1u;
            if (!play && strcmp(lines[i], ":w11=1,1,") == 0 && w20)
                play = i + 1u;
        }
        CHECK(w20 && play > w20);
    }
    CHECK(g_nx_terrain.p[0].item == 1 && g_nx_terrain.p[0].run_s <= 1u);
    nx_terrain_move(1, +1);   /* a jump while it plays: item 2 now */
    CHECK(g_ch[0].prog_slot == NX_SRC_SET && g_nx_terrain.p[0].item == 2);
    nx_terrain_run(0, &t);
    t.run_s = 3599;
    nx_terrain_resume(0, &t);
    run_s(2);
    CHECK(g_nx_terrain.p[0].item == 0 && g_ch[0].prog_slot == NX_SRC_SET);   /* round two */
    nx_terrain_move(1, -1);
    CHECK_EQ(g_nx_terrain.p[0].item, 2);
    nx_terrain_move(1, +1);

    /* the power level: the amplitude of the set */
    nx_terrain_power(1);
    CHECK_EQ(g_nx_terrain.p[0].power, 1);
    CHECK_EQ(g_ch[0].prog.hdr[HDR_AMPL], 250);
    for (unsigned i = 1; i < NX_TERRAIN_POWERS; i++)
        nx_terrain_power(1);
    CHECK_EQ(g_nx_terrain.p[0].power, 0);
    CHECK_EQ(g_ch[0].prog.hdr[HDR_AMPL], 500);

    /* stopping keeps the position and the time */
    run_s(3);
    stop(0);
    run_s(1);
    nx_terrain_run(0, &t);
    CHECK_EQ(t.id, 0);
    CHECK(g_nx_terrain.p[0].item == 0 && g_nx_terrain.p[0].run_s >= 3u);
    uint32_t kept = g_nx_terrain.p[0].run_s;
    CHECK_EQ(nx_terrain_play(0, 1), NX_OK);
    nx_terrain_run(0, &t);
    CHECK_EQ(t.run_s, kept);

    /* a power cut: resumed with its protocol */
    run_s(3);
    power_on(1);
    CHECK_EQ(nx_resume_offer(), 1);
    nxw_run_until(4600);
    CHECK_EQ(nx_resume_accept(), NX_OK);
    CHECK(g_ch[0].prog_slot == NX_SRC_SET && (g_ui_flags & UIF_LOOP_G1));
    nx_terrain_run(0, &t);
    CHECK(t.id == 1 && t.item == 0);

    /* PC mode takes the generators; at its end the set is offered again, with its protocol */
    run_s(2);
    nx_terrain_run(0, &t);
    uint32_t pc_at = t.run_s;
    nxw_pb1 = 1;
    run_s(5);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_pb1 = 0;
    run_s(1);
    CHECK_EQ(nx_resume_offer(), 1);
    CHECK_EQ(nx_resume_accept(), NX_OK);
    CHECK(g_ch[0].prog_slot == NX_SRC_SET && (g_ui_flags & UIF_LOOP_G1));
    nx_terrain_run(0, &t);
    CHECK(t.id == 1 && t.item == 0 && t.run_s >= pc_at && t.run_s <= pc_at + 1u);
    stop(0);
    run_s(1);

    /* Contact on G2: the session counts when its 0 Hz step starts; stopped early it does not */
    CHECK_EQ(nx_terrain_play(1, 2), NX_OK);
    CHECK(g_ch[1].prog_slot == NX_SRC_SET + 2u && !(g_ui_flags & UIF_LOOP_G2));
    run_s(11);
    CHECK_EQ(g_nx_terrain.p[1].item, 0);
    run_s(2);   /* 13 s: the end step */
    CHECK_EQ(g_nx_terrain.p[1].item, 1);
    run_s(4);
    CHECK(!(g_ui_flags & UIF_RUN_G2));
    run_s(1);
    struct nx_terrain_rec r;
    CHECK(nx_kv_get(NX_KEY_TERRAIN, &r, sizeof r) == (int)sizeof r && r.p[1].item == 1 && r.active[1] == 2);
    CHECK_EQ(nx_terrain_play(1, 2), NX_OK);
    run_s(3);
    stop(1);
    run_s(1);
    CHECK_EQ(g_nx_terrain.p[1].item, 1);
    g_nx_terrain.p[1].item = 5;   /* the last session: then complete */
    CHECK_EQ(nx_terrain_play(1, 2), NX_OK);
    CHECK_EQ(g_ch[1].prog_slot, NX_SRC_SET + 3u);
    run_s(21);
    CHECK_EQ(g_nx_terrain.p[1].item, 6);
    CHECK_EQ(nx_terrain_play(1, 2), NX_OK);   /* complete: day 1 again */
    CHECK_EQ(g_nx_terrain.p[1].item, 0);
    stop(1);
    run_s(1);

    /* auto-start goes on with the protocol played last */
    CHECK_EQ(nx_terrain_continue(1), NX_OK);
    CHECK_EQ(g_ch[1].prog_slot, NX_SRC_SET + 2u);
    stop(1);
    run_s(1);

    /* the keys: menu > Chains > the Remote row (the first of the list) > LOOP: its screen,
     * DOWN: the next set, START: plays it on G1 */
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.splash = NX_SPLASH_OFF;
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    power_on(1);
    nxw_run_until(4600);
    nx_scr_menu_preselect(&nx_scr_chains, 0);
    nxw_press(NX_KEY_DOWN, 100);
    nxw_run_ms(250);
    nxw_press(NX_KEY_START, 100);
    nxw_run_ms(250);
    CHECK(nx_ui_top() == &nx_scr_chains);   /* the list opens on the first Terrain protocol */
    nxw_press(NX_KEY_LOOP, 100);
    nxw_run_ms(250);
    CHECK(nx_ui_top() == &nx_scr_terrain);
    unsigned before = g_nx_terrain.p[0].item;
    nxw_press(NX_KEY_DOWN, 100);
    nxw_run_ms(250);
    CHECK_EQ(g_nx_terrain.p[0].item, (before + 1u) % 3u);
    nxw_press(NX_KEY_START, 100);
    nxw_run_ms(400);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK(nx_src_is_set(g_ch[0].prog_slot) && (g_ui_flags & UIF_RUN_G1));
    stop(0);
    run_s(1);

#if NX_FEAT_PRESET
    /* menu > Chains, the rows after the Terrain protocols: the built-in presets. LOOP does nothing there;
     * G2 chooses the generator, START plays the preset on it (this one repeats) and shows it */
    nxw_press(NX_KEY_DOWN, 100);
    nxw_run_ms(250);
    nxw_press(NX_KEY_START, 100);
    nxw_run_ms(250);
    CHECK(nx_ui_top() == &nx_scr_chains);
    for (unsigned i = 0; i < nx_uc_terrain_count(); i++) {
        nxw_press(NX_KEY_DOWN, 100);
        nxw_run_ms(250);
    }
    nxw_press(NX_KEY_LOOP, 100);
    nxw_run_ms(250);
    CHECK(nx_ui_top() == &nx_scr_chains);
    CHECK_EQ(g_ch[1].prog_slot, 0);
    nxw_press(NX_KEY_G2, 100);
    nxw_run_ms(250);
    nxw_press(NX_KEY_START, 100);
    nxw_run_ms(400);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_focus(), 1);
    CHECK_EQ(g_ch[1].prog_slot, NX_SRC_PRESET);
    CHECK((g_ui_flags & UIF_RUN_G2) && (g_ui_flags & UIF_LOOP_G2));
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    stop(1);
    run_s(1);
#endif
}
#endif

void test_nx_terrain(void)
{
    sets();
    playing();
#if NX_FEAT_TERRAIN
    protocols();
#endif
}
