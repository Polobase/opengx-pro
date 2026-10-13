/* nx transitions (src/nx/nx_trans.c) on the host panel with its scroll model: the push schedule,
 * the glass during and after a push in both directions, the backlight dip, the sheet reveal, the
 * toast slide, the region fade and the reduced-motion replacements. */
#include "harness.h"

#include "nx_anim.h"
#include "nx_bl.h"
#include "nx_lcd.h"
#include "nx_panel.h"
#include "nx_render.h"
#include "nx_trans.h"
#include "test_nx.h"

static int s_screen;   /* 0: screen A, 1: screen B */

static uint16_t col_a(int x)
{
    return (uint16_t)(x * 97 + 1);
}

static uint16_t col_b(int x)
{
    return (uint16_t)(0x8000u | (unsigned)(x * 51 + 3));
}

static void draw_screen(nx_gc *gc, void *ctx)
{
    (void)ctx;
    for (int x = gc->clip.x0; x < gc->clip.x1; x++)   /* one colour per column */
        nx_fill(gc, (nx_rect){ (int16_t)x, 0, (int16_t)(x + 1), 240 }, s_screen ? col_b(x) : col_a(x));
}

/* token colours (nx_col), so fades and the scrim apply to them */
#define SHEET_C (nx_render_pal()[NX_PAL_G1])
#define TOAST_C (nx_render_pal()[NX_PAL_ERROR])

static void draw_sheet(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_fill(gc, (nx_rect){ 0, 140, 320, 240 }, nx_col(gc, NX_PAL_G1));
}

static void draw_toast(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_fill(gc, (nx_rect){ 20, 10, 300, 40 }, nx_col(gc, NX_PAL_ERROR));
}

static void setup(void)
{
    nx_reset_world();
    nxp_reset();
    nx_bl_init();
    sim_on_ms(nx_bl_tick_1ms);
    nx_lcd_init();
    nx_render_init();
    nx_anim_set_reduced(0);
    s_screen = 0;
    nx_render_layer(NX_LAYER_SCREEN, draw_screen, NULL);
    nx_render_flush();
}

static void test_schedule(void)
{
    nx_rect c = nx_push_cols(NX_PUSH_LEFT, 1);
    CHECK(c.x0 == 0 && c.x1 == 8 && c.y0 == 0 && c.y1 == 240);
    c = nx_push_cols(NX_PUSH_LEFT, NX_PUSH_STEPS);
    CHECK(c.x0 == 312 && c.x1 == 320);
    c = nx_push_cols(NX_PUSH_RIGHT, 1);
    CHECK(c.x0 == 312 && c.x1 == 320);
    c = nx_push_cols(NX_PUSH_RIGHT, NX_PUSH_STEPS);
    CHECK(c.x0 == 0 && c.x1 == 8);
    CHECK_EQ(nx_push_scroll(NX_PUSH_LEFT, 1), 8);
    CHECK_EQ(nx_push_scroll(NX_PUSH_LEFT, NX_PUSH_STEPS), 0);
    CHECK_EQ(nx_push_scroll(NX_PUSH_RIGHT, 1), 312);
    CHECK_EQ(nx_push_scroll(NX_PUSH_RIGHT, NX_PUSH_STEPS), 0);
    CHECK_EQ(NX_PUSH_STEPS, 40);
}

/* During a push after k steps (scroll s): GRAM columns written so far hold B, the rest A. */
static int glass_during_push(uint8_t dir)
{
    unsigned s = nx_lcd_scroll_pos();
    unsigned done = dir == NX_PUSH_LEFT ? s : (s ? 320u - s : 0u);
    if (!nx_trans_active())
        done = 320;
    for (int x = 0; x < 320; x++) {
        unsigned c = ((unsigned)x + s) % 320u;
        int new_col = dir == NX_PUSH_LEFT ? c < done : c >= 320u - done;
        uint16_t want = new_col ? col_b((int)c) : col_a((int)c);
        if (nxp_at(x, 0) != want || nxp_at(x, 239) != want)
            return 0;
    }
    return 1;
}

static void push(uint8_t dir)
{
    setup();
    uint32_t scrolls0 = g_nx_lcd_stats.scrolls;
    s_screen = 1;
    uint32_t t0 = sim_ms();
    CHECK(nx_trans_push(dir));
    CHECK_EQ(nx_trans_push(dir), 0);                  /* one at a time */
    int ok = 1, midway = 0;
    while (nx_trans_active() && sim_ms() - t0 < 1000u) {
        nx_render_step(2000);
        ok &= glass_during_push(dir);
        if (nx_lcd_scroll_pos() == 160)
            midway = 1;
        sim_advance_ms(1);
    }
    CHECK(ok);
    CHECK(midway);
    CHECK(sim_ms() - t0 >= NX_PUSH_MS && sim_ms() - t0 <= NX_PUSH_MS + 2u);
    CHECK_EQ(nx_lcd_scroll_pos(), 0);
    CHECK_EQ(g_nx_lcd_stats.scrolls - scrolls0, NX_PUSH_STEPS);
    CHECK(glass_during_push(dir));                    /* all B, scroll back at 0 */
    CHECK_EQ(nxp_bad, 0);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        nx_render_step(2000);
        sim_advance_ms(1);
    }
}

static void test_dip(void)
{
    setup();
    uint8_t user = nx_bl_user();
    nx_bl_set_user(user);
    run_ms(5);
    CHECK_EQ(nx_bl_level(), user);
    s_screen = 1;
    CHECK(nx_trans_dip());
    run_ms(30);
    CHECK_EQ(nxp_at(10, 10), col_a(10));              /* the old picture while it darkens */
    CHECK(nx_bl_level() < user);
    run_ms(29);
    CHECK(nx_bl_level() <= user / 2u + 2u);           /* down to half the level (25 % light) */
    CHECK_EQ(nxp_at(10, 10), col_a(10));
    run_ms(5);
    CHECK_EQ(nxp_at(10, 10), col_b(10));              /* rendered in the half light */
    run_ms(130);
    CHECK_EQ(nx_bl_level(), user);
    CHECK(!nx_trans_active());
}

/* nx_trans_cancel() while a dip has the backlight down (phase 0/1) or is waiting for the full
 * render before the up-fade (phase 2) must restore it at once; regression for the gap where only
 * T_PUSH/T_REVEAL/T_SLIDE/T_FADE were restored and a canceled dip left the backlight at half. */
static void test_dip_cancel(void)
{
    setup();
    uint8_t user = nx_bl_user();
    nx_bl_set_user(user);
    run_ms(5);
    s_screen = 1;
    CHECK(nx_trans_dip());
    run_ms(30);                                       /* still darkening: backlight below full */
    CHECK(nx_bl_level() < user);
    nx_trans_cancel();
    run_ms(2);
    CHECK_EQ(nx_bl_level(), user);                     /* restored at once, not parked at half */
    CHECK(!nx_trans_active());
}

static void test_reveal(void)
{
    setup();
    nx_render_layer(NX_LAYER_SHEET, draw_sheet, NULL);
    nx_render_scrim(NX_SCRIM_LEVEL);
    CHECK(nx_trans_reveal(NX_LAYER_SHEET, (nx_rect){ 0, 140, 320, 240 }));
    nx_render_step(2000);
    CHECK_EQ(nxp_at(10, 200), col_a(10));             /* the base first (raw colours: no scrim), sheet hidden */
    int seen_partial = 0;
    for (int i = 0; i < 400 && nx_trans_active(); i++) {
        nx_render_step(2000);
        if (nxp_at(10, 239) == SHEET_C && nxp_at(10, 141) != SHEET_C)
            seen_partial = 1;                         /* bottom rows first */
        sim_advance_ms(1);
    }
    nx_render_flush();
    CHECK(seen_partial);
    CHECK_EQ(nxp_at(10, 141), SHEET_C);
    CHECK(nxp_at(10, 139) != SHEET_C);                /* above the sheet: the base */
    nx_render_scrim(0);
}

static void test_slide_fade(void)
{
    setup();
    nx_render_layer(NX_LAYER_TOAST, draw_toast, NULL);
    nx_rect area = { 20, 10, 300, 40 };
    CHECK(nx_trans_slide(NX_LAYER_TOAST, area, -30, 0, 200));
    nx_render_step(2000);
    CHECK_EQ(nxp_at(100, 5), TOAST_C);                /* starts 30 px higher */
    CHECK(nxp_at(100, 30) != TOAST_C);
    run_ms(250);
    nx_render_flush();
    CHECK(!nx_trans_active());
    CHECK_EQ(nxp_at(100, 30), TOAST_C);
    CHECK_EQ(nxp_at(100, 5), col_a(100));

    CHECK(nx_trans_fade(NX_LAYER_TOAST, area, 1));
    nx_render_step(2000);
    CHECK_EQ(nxp_at(100, 30), nx_render_pal()[NX_PAL_BG]);   /* faded out: the bg colour */
    run_ms(160);
    uint16_t mid = nxp_at(100, 30);
    CHECK(mid != TOAST_C && mid != nx_render_pal()[NX_PAL_BG]);
    run_ms(200);
    nx_render_flush();
    CHECK_EQ(nxp_at(100, 30), TOAST_C);
    CHECK(!nx_trans_active());
}

static void test_reduced(void)
{
    setup();
    nx_anim_set_reduced(1);
    uint32_t scrolls0 = g_nx_lcd_stats.scrolls;
    s_screen = 1;
    CHECK(nx_trans_push(NX_PUSH_LEFT));               /* becomes a dip */
    run_ms(300);
    nx_render_flush();
    CHECK_EQ(g_nx_lcd_stats.scrolls, scrolls0);
    CHECK_EQ(nxp_at(10, 10), col_b(10));
    CHECK(!nx_trans_active());
    nx_render_layer(NX_LAYER_TOAST, draw_toast, NULL);
    CHECK(nx_trans_slide(NX_LAYER_TOAST, (nx_rect){ 20, 10, 300, 40 }, -30, 0, 200));
    CHECK(!nx_trans_active());                        /* instant */
    nx_render_flush();
    CHECK_EQ(nxp_at(100, 30), TOAST_C);
    nx_anim_set_reduced(0);
}

void test_nx_trans(void)
{
    test_schedule();
    push(NX_PUSH_LEFT);
    push(NX_PUSH_RIGHT);
    test_dip();
    test_dip_cancel();
    test_reveal();
    test_slide_fade();
    test_reduced();
}
