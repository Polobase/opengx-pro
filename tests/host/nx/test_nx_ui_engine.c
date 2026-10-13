/* The UI on the engine, end to end with the panel model: what the glass shows after boot, a VM
 * change redraws only its rectangles, a frame draws one consistent snapshot (the frame copies),
 * sheets scrim the screen below, PC mode shows the monitor, factory.c's screen clear repaints,
 * screen off puts the panel to sleep, and the goodbye ends with the panel asleep and the
 * backlight off before the power-off signal. */
#include <setjmp.h>

#include "harness.h"

#include "lcd.h"
#include "link_internal.h"
#include "nx_anim.h"
#include "nx_bl.h"
#include "nx_config.h"
#include "nx_keys.h"
#include "nx_lcd.h"
#include "nx_panel.h"
#include "nx_render.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

static uint16_t pal(uint8_t tok)
{
    return nx_palettes[NX_THEME_DARK][tok];
}

static void press(uint8_t keys, uint32_t hold_ms)
{
    nxw_press(keys, hold_ms);
    nxw_run_ms(150);
}

static void store_program(unsigned slot)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 11;
    p.hdr[HDR_AMPL] = 2000;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = 30;
    p.hdr[HDR_COUNT] = 3;
    p.freq[0] = 7278;
    p.freq[1] = 7288;
    p.freq[2] = 7298;
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
}

/* Boot with the splash off, G1 playing slot 4. */
static void boot_running(void)
{
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.splash = NX_SPLASH_OFF;
    u.motion = 1;   /* reduced: no scope animation, the dirty checks stay exact */
    u.guide = 1;    /* not a first boot: no key guide */
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    nxw_reset();
    nx_store_mount();
    store_program(4);
    nxw_run_until(4600);
    press(NX_KEY_G1, 150);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 150);
    press(NX_KEY_START, 150);
    press(NX_KEY_START, 150);
    nxw_run_ms(4500);   /* 5 s into the 90 s program: the progress bar has a visible fill */
}

static void test_glass(void)
{
    boot_running();
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(nxp_bad, 0);
    CHECK_EQ(nxp_at(160, 10), pal(NX_PAL_SURFACE1));   /* status bar */
    CHECK_EQ(nxp_at(14, 11), pal(NX_PAL_G1));          /* G1 badge */
    CHECK_EQ(nxp_at(2, 224), pal(NX_PAL_G2));          /* the strip's accent bar: the other generator */
    int bar = 0;
    for (int x = 12; x < 20; x++)
        bar |= nxp_at(x, 179) == pal(NX_PAL_G1);       /* progress bar filled in G1's accent */
    CHECK(bar || g_vm.g[0].frac < 256u);

    /* G2 has no program: G2 opens its list; STOP shows G2's dashboard: the badge and the strip
     * swap accents */
    press(NX_KEY_G2, 150);
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_STOP, 150);
    nxw_run_ms(300);
    CHECK_EQ(nx_ui_focus(), 1);
    CHECK_EQ(nxp_at(14, 11), pal(NX_PAL_G2));
    CHECK_EQ(nxp_at(2, 224), pal(NX_PAL_G1));
    CHECK_EQ(nxp_scrolling ? nxp_vsp % 320u : 0u, 0);  /* the push ended with the scroll at 0 */
    press(NX_KEY_G1, 150);
    nxw_run_ms(300);

    /* UP: the quick settings sheet over the scrimmed dashboard */
    press(NX_KEY_UP, 150);
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_quick);
    CHECK(nxp_at(160, 10) != pal(NX_PAL_SURFACE1));    /* scrimmed */
    CHECK_EQ(nxp_at(300, 45), pal(NX_PAL_SURFACE1));   /* the sheet */
    press(NX_KEY_STOP, 150);
    nxw_run_ms(100);
    CHECK_EQ(nxp_at(160, 10), pal(NX_PAL_SURFACE1));

    /* DOWN: the menu (a sheet over the scrimmed dashboard); START on Settings: pushed in (no
     * scrim, the scroll back at 0), STOP back */
    press(NX_KEY_DOWN, 150);
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_menu);
    CHECK(nxp_at(160, 10) != pal(NX_PAL_SURFACE1));   /* scrimmed */
    press(NX_KEY_START, 150);
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_set);
    CHECK_EQ(nxp_at(300, 10), pal(NX_PAL_SURFACE1));   /* its title bar */
    CHECK_EQ(nxp_scrolling ? nxp_vsp % 320u : 0u, 0);
    press(NX_KEY_STOP, 150);
    nxw_run_ms(300);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nxp_at(14, 11), pal(NX_PAL_G1));

    /* factory.c's screen clear repaints from the UI's state */
    lcd_fill_rect(0, 0, 239, 319, 0);
    CHECK_EQ(nx_dirty_count(), 1);
    nxw_run_ms(100);
    CHECK_EQ(nxp_at(14, 11), pal(NX_PAL_G1));

    /* PC mode: the monitor; a frequency the PC sets redraws its whole line; and back */
    nxw_pb1 = 1;
    nxw_run_ms(600);
    CHECK(nx_ui_top() == &nx_scr_pc);
    CHECK_EQ(nxp_at(300, 10), pal(NX_PAL_SURFACE1));
    CHECK_EQ(nx_dirty_count(), 0);
    g_ch[0].pc_freq[0] = 72780000007u;
    nx_ui_tick(nx_ms() + 20u);
    CHECK_EQ(nx_dirty_count(), 1);
    nx_rect line = nx_dirty_get(0);
    CHECK(line.x0 == 0 && line.x1 == 320 && line.y0 == 70 && line.y1 == 96);   /* G1 Out1 */
    nxw_run_ms(100);
    nxw_pb1 = 0;
    nxw_run_ms(600);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nxp_bad, 0);
}

static void test_partial(void)
{
    boot_running();
    press(NX_KEY_START, 150);   /* paused: nothing moves by itself */
    nxw_run_ms(1000);
    CHECK_EQ(nx_dirty_count(), 0);
    uint32_t px = nxp_pixels, frames = g_nx_render_stats.frames;
    nxw_run_ms(500);
    CHECK_EQ(nxp_pixels, px);   /* idle: nothing sent */
    /* one second more on the clock of a paused program: only the meta, the bar and the time */
    g_ch[0].elapsed += 1000u;
    nxw_run_ms(100);
    CHECK(g_nx_render_stats.frames > frames);
    CHECK(nxp_pixels - px > 0u);
    CHECK(nxp_pixels - px < 320u * 80u);   /* far less than a screen */
}

static void test_frame_copy(void)
{
    boot_running();
    nxw_run_ms(1000);
    /* a frame starts: its VM copy stays while the core moves on */
    nxp_async = 1;
    nx_inval_all();
    nx_ui_render_step(100);
    struct nx_vm drawn = g_vm_draw;
    g_ch[0].elapsed += 30000u;   /* the next step */
    nx_ui_tick(nx_ms() + 20u);
    CHECK(g_vm.g[0].step != drawn.g[0].step);
    CHECK(memcmp(&g_vm_draw, &drawn, sizeof drawn) == 0);
    for (unsigned i = 0; i < 2000u && (nxp_complete() || nx_render_busy()); i++)
        nx_ui_render_step(2000);
    nxp_async = 0;
    nxw_run_ms(100);
    CHECK_EQ(g_vm_draw.g[0].step, g_vm.g[0].step);   /* the next frame took the new state */
    CHECK_EQ(nxp_bad, 0);
}

static void test_screen_off(void)
{
    boot_running();
    press(NX_KEY_POWER, 150);
    nxw_run_ms(700);
    CHECK_EQ(nx_bl_level(), 0);
    CHECK_EQ(nxp_sleep, 1);
    CHECK_EQ(nxp_disp_on, 0);
    press(NX_KEY_START, 150);
    nxw_run_ms(200);
    CHECK_EQ(nxp_sleep, 0);
    CHECK_EQ(nxp_disp_on, 1);
    CHECK_EQ(nx_bl_level(), nx_bl_user());
    CHECK_EQ(nxp_at(14, 11), pal(NX_PAL_G1));   /* the picture came back */
    CHECK(g_ui_flags & UIF_RUN_G1);             /* START only woke the screen */
}

static void test_goodbye(void)
{
    boot_running();
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0) {
        nxw_keys = NX_KEY_POWER;
        nxw_run_ms(500);
        CHECK(nx_ui_top() != 0);
        CHECK(nxp_at(160, 10) != pal(NX_PAL_SURFACE1));   /* the power ring over a scrim */
        nxw_run_ms(2500);
        CHECK(0);   /* not reached */
    }
    CHECK_EQ(nxp_sleep, 1);        /* DISPOFF + SLPIN before the PB2 signal */
    CHECK_EQ(nx_bl_level(), 0);
    CHECK_EQ(nxp_bad, 0);
    nxw_keys = 0;
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}

void test_nx_ui_engine(void)
{
    test_glass();
    test_partial();
    test_frame_copy();
    test_screen_off();
    test_goodbye();
}
