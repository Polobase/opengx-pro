/* nx render scheduler (src/nx/nx_render.c) through nx_lcd onto the host panel: band sizes, the
 * dirty-rectangle merge rule, full and partial frames equal to a direct render, the solid-fill
 * fast path, the frame hook, slicing while DMA transfers are in flight, themes, scrim and fade
 * tints, tabular-cell invalidation and a sleeping panel. */
#include "harness.h"

#include "nx_font.h"
#include "nx_lcd.h"
#include "nx_panel.h"
#include "nx_render.h"
#include "test_nx.h"

static uint16_t glass[240 * 320], ref[240 * 320];
static uint16_t s_color;          /* model of the test screen */
static uint16_t s_drawn_color;    /* what the frame hook took */
static unsigned s_frames;

static void on_frame(void)
{
    s_drawn_color = s_color;
    s_frames++;
}

static void draw(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_rrect(gc, (nx_rect){ 20, 20, 140, 80 }, 10, s_drawn_color);
    nx_text(gc, &nx_font_body16, 30, 100, "nx render", nx_col(gc, NX_PAL_TEXT1));
    nx_ring(gc, 250, 150, 40, 8, 0, 700, 1, nx_col(gc, NX_PAL_G2));
}

static void reference(void)
{
    nx_gc gc = { ref, { 0, 0, 320, 240 }, { 0, 0, 320, 240 }, nx_render_pal(), 0, 0, 0, 0 };
    nx_fill(&gc, gc.band, nx_col(&gc, NX_PAL_BG));
    s_drawn_color = s_color;
    draw(&gc, NULL);
}

static int glass_is_ref(void)
{
    nxp_glass(glass);
    return memcmp(glass, ref, sizeof glass) == 0;
}

static void setup(void)
{
    nx_reset_world();
    nxp_reset();
    nx_lcd_init();
    nx_render_init();
    s_frames = 0;
    s_color = 0x051Fu;
    nx_render_on_frame(on_frame);
    nx_render_layer(NX_LAYER_SCREEN, draw, NULL);
}

static void test_bands_and_merge(void)
{
    CHECK_EQ(nx_band_rows(320, 240), 8);
    CHECK_EQ(nx_band_rows(100, 60), 25);
    CHECK_EQ(nx_band_rows(28, 60), 60);
    CHECK_EQ(nx_band_rows(1, 240), 240);
    CHECK_EQ(nx_band_rows(296, 3), 3);

    nx_rect a = { 0, 0, 10, 10 }, b = { 5, 5, 20, 20 }, far = { 200, 200, 210, 210 }, near = { 11, 0, 20, 10 };
    CHECK(nx_rect_mergeable(&a, &b));                 /* overlap */
    CHECK(!nx_rect_mergeable(&a, &far));
    CHECK(nx_rect_mergeable(&a, &near));              /* union 200 <= 1.3 x 190 */

    setup();
    nx_render_flush();
    CHECK_EQ(nx_dirty_count(), 0);
    nx_inval(a);
    nx_inval(b);
    CHECK_EQ(nx_dirty_count(), 1);
    nx_rect m = nx_dirty_get(0);
    CHECK(m.x0 == 0 && m.y0 == 0 && m.x1 == 20 && m.y1 == 20);
    nx_inval((nx_rect){ -5, -5, 0, 400 });           /* off screen: ignored */
    CHECK_EQ(nx_dirty_count(), 1);
    /* 17 scattered rectangles: the list stays at 16 and still covers every one */
    setup();
    nx_render_flush();
    nx_rect rs[17];
    for (int i = 0; i < 17; i++) {
        rs[i] = (nx_rect){ (int16_t)(i % 6 * 50), (int16_t)(i / 6 * 80), (int16_t)(i % 6 * 50 + 4), (int16_t)(i / 6 * 80 + 4) };
        nx_inval(rs[i]);
    }
    CHECK_EQ(nx_dirty_count(), NX_DIRTY_MAX);
    int covered = 1;
    for (int i = 0; i < 17; i++) {
        int in = 0;
        for (unsigned k = 0; k < nx_dirty_count(); k++) {
            nx_rect d = nx_dirty_get(k);
            if (d.x0 <= rs[i].x0 && d.y0 <= rs[i].y0 && d.x1 >= rs[i].x1 && d.y1 >= rs[i].y1)
                in = 1;
        }
        covered &= in;
    }
    CHECK(covered);
}

static void test_frames(void)
{
    setup();
    CHECK(nx_render_flush());
    reference();
    CHECK(glass_is_ref());
    CHECK_EQ(s_frames, 1);
    CHECK_EQ(g_nx_render_stats.frames, 1);
    CHECK(g_nx_render_stats.fills > 0);               /* the empty bands went out as fills */
    CHECK_EQ(nxp_bad, 0);

    /* a partial update sends only the invalidated area, drawn from the new state */
    s_color = 0xF81Fu;
    uint32_t px0 = nxp_pixels;
    nx_inval((nx_rect){ 20, 20, 140, 80 });
    CHECK(nx_render_flush());
    reference();
    CHECK(glass_is_ref());
    CHECK_EQ(nxp_pixels - px0, 120u * 60u);
    CHECK_EQ(s_frames, 2);

    /* the state a frame draws is the one the hook took, even if the model changes mid-frame */
    nxp_async = 1;
    nx_inval_all();
    nx_render_step(100000);
    s_color = 0x07E0u;                                /* after the frame started */
    for (int i = 0; i < 1000 && (nx_render_busy() || !nx_lcd_idle()); i++) {
        nxp_complete();
        nx_render_step(2000);
    }
    nxp_async = 0;
    s_color = 0xF81Fu;
    reference();
    CHECK(glass_is_ref());                            /* still the colour of the frame start */
    s_color = 0x07E0u;
    nx_inval_all();
    nx_render_flush();
    reference();
    CHECK(glass_is_ref());

    /* nothing but background: the whole rectangle goes out as one fill */
    setup();
    nx_render_layer(NX_LAYER_SCREEN, NULL, NULL);
    uint32_t fills = g_nx_lcd_stats.fills, bands = g_nx_lcd_stats.bands;
    nx_render_flush();
    CHECK_EQ(g_nx_lcd_stats.fills - fills, 1);
    CHECK_EQ(g_nx_lcd_stats.bands - bands, 0);
    CHECK_EQ(nxp_at(160, 120), nx_render_pal()[NX_PAL_BG]);
}

static void test_slices(void)
{
    /* with transfers in flight a slice returns at once; the frame finishes as they complete */
    setup();
    nxp_async = 1;
    int steps = 0;
    while (nx_render_busy() && steps < 5000) {
        nx_render_step(2000);
        nxp_complete();
        steps++;
    }
    nxp_async = 0;
    CHECK(steps > 2);                                 /* slices returned while transfers ran */
    CHECK(steps < 5000);
    reference();
    CHECK(glass_is_ref());
    CHECK_EQ(nxp_bad, 0);
}

static void test_themes_and_tints(void)
{
    setup();
    nx_render_flush();
    nx_render_theme(NX_THEME_LIGHT);
    CHECK_EQ(nx_dirty_count(), 1);
    nx_render_flush();
    CHECK_EQ(nxp_at(5, 5), nx_palettes[NX_THEME_LIGHT][NX_PAL_BG]);
    nx_render_theme(NX_THEME_DARK);
    nx_render_flush();

    /* scrim: bg and the screen layer's token colours tinted towards bg (raw colours are not);
     * a faded layer likewise. (285, 150) lies on the ring (token g2). */
    uint16_t g2 = nx_palettes[NX_THEME_DARK][NX_PAL_G2], bg = nx_palettes[NX_THEME_DARK][NX_PAL_BG];
    CHECK_EQ(nxp_at(285, 150), g2);
    nx_render_scrim(32);
    nx_inval_all();
    nx_render_flush();
    CHECK_EQ(nxp_at(285, 150), bg);
    CHECK_EQ(nxp_at(80, 50), s_color);
    nx_render_scrim(NX_SCRIM_LEVEL);
    nx_inval_all();
    nx_render_flush();
    CHECK_EQ(nxp_at(285, 150), nx_mix(g2, bg, NX_SCRIM_LEVEL));
    nx_render_scrim(0);
    nx_render_layer_fade(NX_LAYER_SCREEN, 16);
    nx_inval_all();
    nx_render_flush();
    CHECK_EQ(nxp_at(285, 150), nx_mix(g2, bg, 16));
    nx_render_layer_fade(NX_LAYER_SCREEN, 32);

    /* tabular cells: only the digit that changed */
    nx_render_flush();
    nx_inval_text(&nx_font_num48, 10, 60, "727.000", "728.000");
    CHECK_EQ(nx_dirty_count(), 1);
    nx_rect d = nx_dirty_get(0);
    int16_t cx[8];
    nx_text_cells(&nx_font_num48, "728.000", 10, cx, 7);
    CHECK(d.x0 == cx[2] && d.x1 == cx[3] && d.y0 == 60 && d.y1 == 60 + nx_font_num48.height);
    nx_render_flush();
    nx_inval_text(&nx_font_num48, 10, 60, "99.5", "100.5");
    d = nx_dirty_get(0);
    CHECK(d.x0 == 10 && d.x1 >= 10 + (int)nx_font_text_width(&nx_font_num48, "100.5"));
    nx_render_flush();
}

static void test_sleeping_panel(void)
{
    setup();
    nx_render_flush();
    nx_lcd_power(NX_LCD_SLEEP);
    sim_advance_ms(200);
    nx_render_step(2000);
    nx_render_step(2000);
    CHECK_EQ(nx_lcd_power_state(), NX_LCD_SLEEP);
    uint32_t px0 = nxp_pixels;
    s_color = 0x001Fu;
    nx_inval_all();
    CHECK_EQ(nx_render_step(2000), 0);                /* nothing to do while asleep */
    CHECK(!nx_render_busy());
    CHECK_EQ(nxp_pixels, px0);
    CHECK_EQ(nx_dirty_count(), 1);                    /* kept for the wake-up */
    nx_lcd_power(NX_LCD_BLANK);
    sim_advance_ms(200);
    nx_render_step(2000);                             /* SLPOUT */
    sim_advance_ms(5);
    nx_render_flush();                                /* rendered in the dark */
    CHECK_EQ(nxp_disp_on, 0);
    nx_lcd_power(NX_LCD_ON);
    nx_render_step(2000);
    CHECK_EQ(nxp_disp_on, 1);
    reference();
    CHECK(glass_is_ref());
}

void test_nx_render(void)
{
    test_bands_and_merge();
    test_frames();
    test_slices();
    test_themes_and_tints();
    test_sleeping_panel();
}
