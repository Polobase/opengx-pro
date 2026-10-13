/* The UI's key model end to end on the host board model (tools/scenarios/keymaps/nx.json,
 * docs/nx/key-model.md): raw keys -> SysTick sampler -> gesture engine -> UI screens -> action
 * outbox -> core -> runner -> queue -> wire, and the screen stack after each step. The
 * key-to-line times are what diff.py --timing anchor compares with the original. Also: the key
 * lock, PC-mode refusal, the program detail from the library and the dashboard (its step for the
 * target, START on the target's own program), FINISHED and loop passes, screen off and dimming,
 * settings persistence. */
#include "harness.h"

#include "link_internal.h"
#include "nx_bl.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_keys.h"
#include "nx_panel.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

extern struct nx_action g_ui_actlog[16];
extern unsigned g_ui_actn;

static char lines[2][64][32];
static unsigned nlines[2];

static void grab(void)
{
    for (unsigned ch = 0; ch < 2u; ch++)
        nlines[ch] = nxw_lines(ch, lines[ch], 64);
}

static void store_program(unsigned slot, unsigned count, unsigned dwell)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 41;
    p.hdr[HDR_AMPL] = 2000;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = dwell;
    p.hdr[HDR_COUNT] = count;
    p.freq[0] = 3750007;
    p.freq[1] = 1000000007;
    uint16_t params[2 * PROGRAM_STEPS];
    memset(params, 0, sizeof params);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
}

/* Press keys at the current time, hold, release, run 250 ms; returns the press time. */
static uint64_t press(uint8_t keys, uint32_t hold_ms)
{
    uint64_t t = sim_us;
    nxw_press(keys, hold_ms);
    nxw_run_ms(150);
    return t;
}

/* A boot with the default settings (the splash, then the dashboard; the key guide was seen at
 * the first boot), slot 4 stored, to 4.6 s. */
static void boot(void)
{
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    nxw_reset();
    nx_store_mount();   /* as main_nx.c: an erased region, formatted on the first write */
    store_program(4, 2, 30);
    nxw_run_until(4600);
}

static void test_traffic(void)
{
    boot();
    nxw_clear();
    CHECK(nx_ui_top() == &nx_scr_dash);

    /* open_list ch 1: G1 (focused already: its library), G1 again keeps G1 as the target */
    press(NX_KEY_G1, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_G1, 150);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    /* nav_down x3, confirm: slot 4 into G1; both links resend, as in the original */
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    uint64_t t = press(NX_KEY_START, 150);
    CHECK_EQ(g_ch[0].prog_slot, 4);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_focus(), 0);
    grab();
    CHECK(nlines[0] == 10u && strcmp(lines[0][0], ":w24=3750007,") == 0 && strcmp(lines[0][4], ":w20=41,") == 0);
    CHECK(nlines[1] == 10u && strcmp(lines[1][0], ":w24=0,") == 0);
    CHECK(nxw_line_us[0][0] - t < 30000u);       /* debounce (2 samples) + one loop pass */

    /* play_pause: START plays the focused G1, again pauses (idle configuration) */
    nxw_clear();
    t = press(NX_KEY_START, 150);
    CHECK(g_ui_flags & UIF_RUN_G1);
    grab();
    CHECK(nlines[0] == 3u && strcmp(lines[0][1], ":w11=1,1,") == 0 && strcmp(lines[0][2], ":w12=0,0,") == 0);
    CHECK(nxw_line_us[0][0] - t < 30000u);
    CHECK_EQ(nlines[1], 0);
    nxw_clear();
    press(NX_KEY_START, 150);
    CHECK_EQ(g_ui_flags & UIF_RUN_G1, 0);
    grab();
    CHECK(nlines[0] == 10u && strcmp(lines[0][1], ":w11=0,0,") == 0);

    /* stop: STOP acts when released (it has a hold meaning), so nothing before the release */
    nxw_clear();
    nxw_keys = NX_KEY_STOP;
    nxw_run_ms(150);
    CHECK_EQ(tx_len[0], 0);
    nxw_keys = 0;
    uint64_t released = sim_us;
    nxw_run_ms(200);
    grab();
    CHECK_EQ(nlines[0], 10);
    CHECK(nxw_line_us[0][0] >= released && nxw_line_us[0][0] - released < 30000u);
    CHECK_EQ(g_ch[0].elapsed, 1);

    /* loop: LOOP tapped on the stopped G1 plays it looped */
    nxw_clear();
    press(NX_KEY_LOOP, 150);
    CHECK((g_ui_flags & (UIF_RUN_G1 | UIF_LOOP_G1)) == (UIF_RUN_G1 | UIF_LOOP_G1));
    grab();
    CHECK(nlines[0] == 3u && strcmp(lines[0][1], ":w11=1,1,") == 0);
    /* LOOP while running: only the loop flag (nothing sent) */
    nxw_clear();
    press(NX_KEY_LOOP, 150);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_ui_flags & UIF_LOOP_G1, 0);
    CHECK_EQ(tx_len[0], 0);

    /* stop_all: STOP held 1.2 s, then released: both generators stop at the release; the hold
     * ring shows while it is held */
    nxw_clear();
    nxw_keys = NX_KEY_STOP;
    nxw_run_ms(1200);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    nxw_keys = 0;
    released = sim_us;
    nxw_run_ms(200);
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
    grab();
    CHECK(nlines[0] == 10u && nlines[1] == 10u);
    CHECK(nxw_line_us[0][0] - released < 30000u && nxw_line_us[1][0] - released < 30000u);
    /* STOP held between 0.5 and 1 s: nothing */
    nxw_clear();
    press(NX_KEY_STOP, 700);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);

    /* G2 without a program opens G2's program list at once (the original's CH1 + G2); STOP shows
     * G2's dashboard without loading; in the list G1/G2 choose the target; nothing is sent */
    nxw_clear();
    press(NX_KEY_G2, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    CHECK_EQ(nx_ui_focus(), 0);
    press(NX_KEY_STOP, 150);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    CHECK_EQ(nx_ui_focus(), 1);
    press(NX_KEY_START, 150);   /* G2's empty dashboard: START opens its list */
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_G1, 150);
    press(NX_KEY_STOP, 150);    /* the target was switched to G1: back to G1's dashboard */
    CHECK_EQ(nx_ui_focus(), 0);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    /* G2's list loads into G2 and shows G2; its selection is still slot 4, as in the original */
    press(NX_KEY_G2, 150);
    press(NX_KEY_START, 150);
    CHECK_EQ(g_ch[1].prog_slot, 4);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_focus(), 1);

    /* POWER tapped: screen off (a 600 ms fade); the next key only wakes the screen */
    press(NX_KEY_POWER, 150);
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), 0);
    nxw_clear();
    press(NX_KEY_START, 150);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
    nxw_run_ms(200);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK_EQ(nx_keys_power_off_due(), 0);

    /* PC mode: the PC monitor; generator keys do nothing, the UI never asks the core */
    nxw_pb1 = 1;
    nxw_run_ms(600);
    CHECK_EQ(g_ui_state, ST_PC);
    CHECK(nx_ui_top() == &nx_scr_pc);
    unsigned acts = g_ui_actn;
    nxw_clear();
    press(NX_KEY_START, 150);
    press(NX_KEY_LOOP, 150);
    press(NX_KEY_STOP, 150);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK_EQ(g_ui_actn, acts);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 0);
    /* an action that reaches the core anyway: the core's one refusal, before any queue */
    CHECK_EQ(nx_ui_act(NXA_PLAY, 0, 0, 0), NX_REFUSED_PC);
    nxw_run_ms(100);
    CHECK_EQ(g_ui_flags & UIF_RUN_G1, 0);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 0);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    nxw_pb1 = 0;
    nxw_run_ms(600);
    CHECK(nx_ui_top() == &nx_scr_dash);

    /* a key pressed in PC mode and still held when PC mode ends does nothing on the dashboard
     * (the original ignores a key in the pass that leaves PC mode): no action, no page opens */
    static const uint8_t k_held[] = { NX_KEY_START, NX_KEY_LOOP, NX_KEY_STOP, NX_KEY_G1, NX_KEY_G2,
                                      NX_KEY_UP, NX_KEY_DOWN };
    for (unsigned i = 0; i < sizeof k_held; i++) {
        nxw_pb1 = 1;
        nxw_run_ms(600);
        CHECK(nx_ui_top() == &nx_scr_pc);
        uint8_t focus = nx_ui_focus();
        acts = g_ui_actn;
        nxw_keys = k_held[i];
        nxw_run_ms(300);
        nxw_pb1 = 0;
        nxw_run_ms(1800);          /* PC mode ends under the held key, which stays down */
        nxw_keys = 0;
        nxw_run_ms(600);
        CHECK(nx_ui_top() == &nx_scr_dash);
        CHECK_EQ(nx_ui_depth(), 1);
        CHECK_EQ(nx_ui_focus(), focus);
        CHECK_EQ(g_ui_actn, acts);
        CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
        if (nx_ui_top() != &nx_scr_dash)   /* the next round starts from the dashboard */
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_NONE);
    }
}

static void test_lock(void)
{
    boot();
    press(NX_KEY_G1, 150);   /* the library */
    press(NX_KEY_START, 150);   /* slot 1 is empty: it loads like any other, as the empty program */
    CHECK_EQ(g_ch[0].prog_slot, 1);
    CHECK(nx_ui_top() == &nx_scr_dash);
    press(NX_KEY_G1, 150);   /* the library again, still on slot 1 */
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);
    CHECK_EQ(g_ch[0].prog_slot, 4);

    /* G1+G2 held 1.5 s: locked; START does nothing, the lock card shows */
    press(NX_KEY_G1 | NX_KEY_G2, 1700);
    CHECK(nx_ui_locked());
    nxw_clear();
    unsigned acts = g_ui_actn;
    press(NX_KEY_START, 150);
    CHECK_EQ(tx_len[0], 0);
    CHECK_EQ(g_ui_actn, acts);
    /* POWER still switches the screen */
    press(NX_KEY_POWER, 150);
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), 0);
    press(NX_KEY_UP, 150);   /* wakes only */
    nxw_run_ms(200);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK(nx_ui_locked());
    /* START+STOP held 1.5 s: unlocked, and the chord's keys do nothing on their own */
    press(NX_KEY_START | NX_KEY_STOP, 1700);
    CHECK(!nx_ui_locked());
    CHECK_EQ(tx_len[0], 0);
    nxw_clear();
    press(NX_KEY_START, 150);
    CHECK(g_ui_flags & UIF_RUN_G1);
    grab();
    CHECK_EQ(nlines[0], 3);
}

static void test_detail_and_finish(void)
{
    boot();
    press(NX_KEY_G1, 150);
    nxw_clear();
    press(NX_KEY_LOOP, 700);   /* held on the empty slot 1: its detail, an empty list */
    CHECK(nx_ui_top() == &nx_scr_detail);
    press(NX_KEY_STOP, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_LOOP, 700);   /* held: the program detail of slot 4 */
    CHECK(nx_ui_top() == &nx_scr_detail);
    press(NX_KEY_G2, 150);     /* target G2 */
    nxw_clear();
    unsigned acts = g_ui_actn;
    press(NX_KEY_START, 150);  /* load and play on G2 */
    CHECK_EQ(g_ui_actn, acts + 2u);
    CHECK_EQ(g_ui_actlog[acts].type, NXA_LOAD);
    CHECK_EQ(g_ui_actlog[acts].ch, 1);
    CHECK_EQ(g_ui_actlog[acts].id, 4);
    CHECK_EQ(g_ui_actlog[acts + 1u].type, NXA_PLAY);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_focus(), 1);
    CHECK(g_ui_flags & UIF_RUN_G2);
    grab();
    CHECK(nlines[1] >= 3u);

    /* 2 steps of 30 s: the runner's end sets FINISHED (a latch of the UI) */
    nxw_run_ms(61000);
    CHECK_EQ(g_ui_flags & UIF_RUN_G2, 0);
    CHECK_EQ(g_vm.g[1].state, NXG_FINISHED);
    press(NX_KEY_START, 150);  /* again, from step 1 */
    CHECK(g_ui_flags & UIF_RUN_G2);
    CHECK_EQ(g_vm.g[1].state, NXG_RUNNING);

    /* LOOP held on the dashboard: the detail of G2's program; STOP back, nothing sent */
    nxw_clear();
    acts = g_ui_actn;
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_detail);
    press(NX_KEY_STOP, 150);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(g_ui_actn, acts);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK(g_ui_flags & UIF_RUN_G2);
    /* G1 has no program: G1 opens its list, STOP shows G1's empty dashboard, where LOOP held does
     * nothing */
    press(NX_KEY_G1, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_STOP, 150);
    CHECK_EQ(nx_ui_focus(), 0);
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    press(NX_KEY_G2, 150);

    /* looped: every pass counts */
    press(NX_KEY_LOOP, 150);   /* repeat on while running */
    CHECK(g_ui_flags & UIF_LOOP_G2);
    nxw_run_ms(61000);
    CHECK(g_ui_flags & UIF_RUN_G2);
    CHECK_EQ(g_vm.g[1].passes, 1);
}

/* The detail's accent bar (x 5) on row `row` of its list (rows of 30 px from y 52). */
static uint16_t detail_bar(unsigned row)
{
    return nxp_at(5, 52 + 30 * (int)row + 15);
}

static void test_detail_target(void)
{
    const uint16_t *pal = nx_palettes[NX_THEME_DARK];
    boot();
    store_program(5, 2, 30);
    press(NX_KEY_G1, 150);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);   /* slot 4 into G1 */
    press(NX_KEY_START, 150);   /* play */
    nxw_run_ms(30500);          /* step 2 of 2 */
    press(NX_KEY_G2, 150);      /* G2 focused */
    press(NX_KEY_G2, 150);      /* its library, still on slot 4 */
    press(NX_KEY_START, 150);
    press(NX_KEY_START, 150);   /* G2 plays slot 4 from step 1 */
    CHECK(g_vm.g[0].slot == 4 && g_vm.g[0].step == 2 && g_vm.g[1].slot == 4 && g_vm.g[1].step == 1);

    /* both play slot 4: LOOP held on a dashboard selects that generator's step */
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_detail);
    CHECK_EQ(detail_bar(0), pal[NX_PAL_G2]);
    CHECK(detail_bar(1) != pal[NX_PAL_G2]);
    press(NX_KEY_STOP, 150);
    press(NX_KEY_G1, 150);      /* G1 focused */
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_detail);
    CHECK_EQ(detail_bar(1), pal[NX_PAL_G1]);
    CHECK(detail_bar(0) != pal[NX_PAL_G1]);

    /* START on the target's own program: back to its dashboard, nothing loaded or restarted */
    nxw_clear();
    unsigned acts = g_ui_actn;
    press(NX_KEY_START, 150);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    CHECK_EQ(nx_ui_focus(), 0);
    CHECK_EQ(g_ui_actn, acts);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_vm.g[0].step, 2);

    /* its own program paused: START in the detail resumes it where it was, without a reload */
    press(NX_KEY_START, 150);   /* the dashboard: pause G1 */
    CHECK_EQ(g_vm.g[0].state, NXG_PAUSED);
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_detail);
    CHECK_EQ(detail_bar(1), pal[NX_PAL_G1]);   /* paused G1 plays it too: its step, not G2's */
    acts = g_ui_actn;
    press(NX_KEY_START, 150);
    CHECK_EQ(g_ui_actn, acts + 1u);
    CHECK_EQ(g_ui_actlog[acts].type, NXA_PLAY);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(g_vm.g[0].state, NXG_RUNNING);
    CHECK_EQ(g_vm.g[0].step, 2);

    /* G1 stopped (slot 4 loaded, step 1) while G2 plays it: the detail from G1 opens at G2's step */
    nxw_run_ms(28000);          /* G2 into its step 2; G1 finishes */
    press(NX_KEY_STOP, 150);    /* G1 stopped */
    CHECK(g_vm.g[1].state == NXG_RUNNING && g_vm.g[1].step == 2);
    CHECK(g_vm.g[0].state != NXG_RUNNING && g_vm.g[0].state != NXG_PAUSED);
    press(NX_KEY_LOOP, 700);
    CHECK(nx_ui_top() == &nx_scr_detail);
    CHECK_EQ(detail_bar(1), pal[NX_PAL_G1]);
    press(NX_KEY_STOP, 150);

    /* another slot for the same target: START loads and plays it */
    press(NX_KEY_G1, 150);      /* G1's library, on slot 4 */
    press(NX_KEY_DOWN, 150);
    press(NX_KEY_LOOP, 700);    /* the detail of slot 5 */
    CHECK(nx_ui_top() == &nx_scr_detail);
    acts = g_ui_actn;
    press(NX_KEY_START, 150);
    CHECK_EQ(g_ui_actn, acts + 2u);
    CHECK_EQ(g_ui_actlog[acts].type, NXA_LOAD);
    CHECK_EQ(g_ui_actlog[acts].ch, 0);
    CHECK_EQ(g_ui_actlog[acts].id, 5);
    CHECK_EQ(g_ui_actlog[acts + 1u].type, NXA_PLAY);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK(g_vm.g[0].slot == 5 && g_vm.g[0].step == 1 && g_vm.g[0].state == NXG_RUNNING);
}

static void test_screen_power(void)
{
    boot();
    press(NX_KEY_UP, 150);   /* quick settings */
    CHECK(nx_ui_top() == &nx_scr_quick);
    press(NX_KEY_STOP, 150);
    /* dim after 1 min without a key, off 5 min later (defaults) */
    nxw_run_ms(59000);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    nxw_run_ms(3000);
    CHECK(nx_bl_level() < nx_bl_user());
    CHECK(nx_bl_level() > 0);
    nxw_run_ms(301000);
    CHECK_EQ(nx_bl_level(), 0);
    press(NX_KEY_DOWN, 150);   /* wakes only: no settings */
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    /* a finished program wakes a dimmed screen */
    store_program(4, 1, 1);
    press(NX_KEY_G1, 150);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);
    press(NX_KEY_START, 150);   /* play: 1 step of 1 s */
    nxw_run_ms(61500);          /* dimmed at 60 s, the program ended long before */
    CHECK_EQ(g_vm.g[0].state, NXG_FINISHED);
    press(NX_KEY_POWER, 150);   /* screen off */
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), 0);
    nx_ui_post(NXE_FINISHED, 0, 0);   /* attention: wakes the screen */
    nxw_run_ms(600);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK_EQ(nxp_sleep, 0);
}

static void test_settings_saved(void)
{
    boot();
    press(NX_KEY_UP, 150);    /* quick settings: brightness row */
    press(NX_KEY_G2, 150);    /* 70 % -> 80 % */
    CHECK_EQ(g_uiset.bright, 8);
    CHECK_EQ(nx_bl_user(), nx_uiset_level(&g_uiset));
    struct nx_settings s;
    CHECK(nx_settings_load(&s) != NX_OK || s.ui[2] != 8);   /* not written while the sheet is open */
    press(NX_KEY_STOP, 150);  /* closed: written once, 3 s after the last key */
    nxw_run_ms(2000);
    CHECK(nx_settings_load(&s) != NX_OK || s.ui[2] != 8);
    nxw_run_ms(1500);
    CHECK_EQ(nx_settings_load(&s), NX_OK);
    CHECK_EQ(s.ui[0], NX_UISET_VER);
    CHECK_EQ(s.ui[2], 8);
    /* the next boot starts with them */
    memcpy(g_nx_settings.ui, s.ui, sizeof s.ui);
    nxw_reset();
    nx_store_mount();
    nxw_run_until(4600);
    CHECK_EQ(g_uiset.bright, 8);
    CHECK(nx_ui_top() == &nx_scr_dash);
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}

/* The first boot: the key guide after the splash; the first key closes it and acts on the
 * dashboard (G1 opens G1's program list), and the guide is not shown again. */
static void test_first_boot_guide(void)
{
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);   /* no settings yet */
    nxw_reset();
    nx_store_mount();
    store_program(4, 2, 30);
    nxw_run_until(4600);
    CHECK(nx_ui_top() == &nx_scr_guide_boot);
    CHECK_EQ(g_uiset.guide, 0);
    press(NX_KEY_G1, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    CHECK_EQ(nx_ui_depth(), 2);
    CHECK_EQ(g_uiset.guide, 1);
    /* from Settings > Keys & lock > Show key guide: START closes it */
    press(NX_KEY_STOP, 150);
    press(NX_KEY_DOWN, 150);               /* the menu, on Settings */
    press(NX_KEY_START, 150);              /* Settings */
    press(NX_KEY_DOWN, 150);               /* Keys & lock */
    press(NX_KEY_START, 150);
    for (int i = 0; i < 4; i++)            /* key repeat, auto-lock, resume, original keys: to Show key guide */
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);
    nxw_run_ms(400);
    CHECK(nx_ui_top() == &nx_scr_guide);
    press(NX_KEY_START, 150);
    nxw_run_ms(400);
    CHECK(nx_ui_top() == &nx_scr_set);
}

/* Settings > Display > When idle: Night view. After dim + off time the night view shows at 3 %
 * backlight; the first key only wakes it. */
static void test_night_view(void)
{
    boot();
    g_uiset.idle = NX_IDLE_AMBIENT;
    nxw_run_ms(62000);          /* dimmed */
    CHECK(nx_bl_level() < nx_bl_user());
    nxw_run_ms(301000);         /* 5 min more: the night view */
    CHECK_EQ(nx_bl_level(), 44);
    CHECK_EQ(nxp_sleep, 0);
    unsigned acts = g_ui_actn;
    press(NX_KEY_START, 150);   /* wakes only: nothing opens or plays */
    nxw_run_ms(300);
    CHECK_EQ(g_ui_actn, acts);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    g_uiset.idle = NX_IDLE_OFF;
}

/* Settings > Reset all settings: START only asks, START held 1 s resets (the key guide stays
 * seen). */
static void test_reset_settings(void)
{
    boot();
    g_uiset.theme = NX_THEME_LIGHT;
    g_uiset.repeat = 2;
    press(NX_KEY_DOWN, 150);   /* the menu, on Settings */
    press(NX_KEY_START, 150);  /* Settings */
    for (int i = 0; i < 4; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);
    CHECK(nx_ui_top() == &nx_scr_confirm);
    CHECK_EQ(g_uiset.theme, NX_THEME_LIGHT);   /* not yet */
    press(NX_KEY_START, 1200);
    CHECK(nx_ui_top() == &nx_scr_set);
    CHECK_EQ(g_uiset.theme, NX_THEME_DARK);
    CHECK_EQ(g_uiset.repeat, 1);
    CHECK_EQ(g_uiset.guide, 1);
}

/* The original's keys (Settings > Keys & lock > Original key behaviour): POWER short on the
 * dashboard stops both, START pauses with repeat off, LOOP while running pauses with repeat on;
 * without it, POWER short switches the screen off. Also the "Stopped" status and the program
 * list's remembered window. */
static void test_original_keys(void)
{
    boot();
    g_uiset.origkeys = 1;
    nx_ui_apply();
    press(NX_KEY_G1, 150);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);          /* slot 4 into G1 */
    press(NX_KEY_LOOP, 150);           /* play looped */
    CHECK((g_ui_flags & UIF_RUN_G1) && (g_ui_flags & UIF_LOOP_G1));
    press(NX_KEY_START, 150);          /* the original: pause, repeat cleared */
    CHECK(!(g_ui_flags & UIF_RUN_G1) && !(g_ui_flags & UIF_LOOP_G1));
    press(NX_KEY_START, 150);          /* play, repeat off */
    press(NX_KEY_LOOP, 150);           /* running: the original pauses with repeat on */
    CHECK(!(g_ui_flags & UIF_RUN_G1) && (g_ui_flags & UIF_LOOP_G1));
    press(NX_KEY_START, 150);
    CHECK(g_ui_flags & UIF_RUN_G1);
    press(NX_KEY_POWER, 150);          /* POWER short: both stop, the screen stays on */
    nxw_run_ms(700);
    CHECK_EQ(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2), 0);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK(g_vm.g[0].state == NXG_READY && g_vm.g[0].stopped);   /* "STOPPED", as the original */
    g_uiset.origkeys = 0;
    nx_ui_apply();
    press(NX_KEY_POWER, 150);          /* today's keys: screen off */
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), 0);
    /* w64 on from the PC wakes the switched-off screen */
    g_backlight_duty = 8;
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK_EQ(nxp_sleep, 0);
    /* the program list keeps its window: slot 20 selected, window from slot 14, as it was left */
    press(NX_KEY_G1, 150);             /* G1 (loaded, shown): its list */
    CHECK(nx_ui_top() == &nx_scr_lib);
    for (int i = 0; i < 16; i++)
        press(NX_KEY_DOWN, 150);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_UP, 150);
    press(NX_KEY_STOP, 150);
    press(NX_KEY_G1, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_UP, 150);             /* the window did not move: the bar moves up one row */
    press(NX_KEY_STOP, 150);
}

void test_nx_ui(void)
{
    test_original_keys();
    test_first_boot_guide();
    test_night_view();
    test_reset_settings();
    test_traffic();
    test_lock();
    test_detail_and_finish();
    test_detail_target();
    test_screen_power();
    test_settings_saved();
}
