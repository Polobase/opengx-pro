/* The list component (src/nx/nx_comp.c): selection, viewport, the highlight slide and the
 * viewport scroll with their invalidations, repeats and reduced motion without animation, and
 * the marquee's invalidation steps; texts that do not fit end in the fonts' own "…"; a title
 * that ends before the target label of its bar. */
#include "harness.h"

#include "nx_anim.h"
#include "nx_comp.h"
#include "nx_render.h"
#include "test_nx.h"

static struct nx_kev key(uint8_t type, uint8_t keys)
{
    struct nx_kev e = { type, keys, 0, 0 };
    return e;
}

static int dirty_covers(nx_rect r)
{
    for (unsigned i = 0; i < nx_dirty_count(); i++) {
        nx_rect d = nx_dirty_get(i);
        if (d.x0 <= r.x0 && d.y0 <= r.y0 && d.x1 >= r.x1 && d.y1 >= r.y1)
            return 1;
    }
    return 0;
}

static void clean(void)
{
    nx_render_init();
    while (nx_dirty_count())   /* nx_render_init marks the screen dirty: take it */
        nx_render_restart();
}

static uint16_t s_cut[320 * 24], s_whole[320 * 24];

/* nx_text_fit: a text that fits is drawn whole; a longer one keeps whole glyphs and ends in the
 * font's "…" (both text fonts carry the glyph), never wider than max_w. */
static void test_fit(void)
{
    CHECK(nx_font_glyph(&nx_font_body16, NX_CH_hellip) != 0);
    CHECK(nx_font_glyph(&nx_font_cap13, NX_CH_hellip) != 0);
    const char *s = "Pain Relief (general) - long program name";
    nx_fit_t f = nx_font_fit(&nx_font_body16, s, 80);
    CHECK(f.ellipsis && f.n > 0 && f.width <= 80u && s[f.n]);
    CHECK(f.width == nx_font_text_width_n(&nx_font_body16, s, f.n)
                     + nx_font_text_width(&nx_font_body16, nx_font_ellipsis(&nx_font_body16)));
    f = nx_font_fit(&nx_font_body16, "Sleep", 80);
    CHECK(!f.ellipsis && f.n == 5u && f.width == nx_font_text_width(&nx_font_body16, "Sleep"));
}

/* The library's and the detail's title bar: a long title ends before the "Run on" label (its ink,
 * the difference to the bar without a title, stays left of the label with a gap). */
static void test_title_target(void)
{
    const nx_rect band = { 0, 0, 320, 24 };
    nx_gc a = { s_cut, band, band, nx_palettes[NX_THEME_DARK], 0, 0, 0, 0 };
    nx_gc b = { s_whole, band, band, nx_palettes[NX_THEME_DARK], 0, 0, 0, 0 };
    nx_comp_title_target(&a, "03 \x88 Pain Relief (general) - long program name", NX_S_DETAIL_RUN_ON, 1);
    nx_comp_title_target(&b, "", NX_S_DETAIL_RUN_ON, 1);
    int label_x0 = NX_LCD_W - NX_M - 64 - 6 - (int)nx_font_text_width(&nx_font_cap13, nx_str(NX_S_DETAIL_RUN_ON));
    int title = 0, into_gap = 0;
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 320; x++)
            if (s_cut[y * 320 + x] != s_whole[y * 320 + x]) {
                title = 1;
                into_gap |= x >= label_x0 - 4;
            }
    CHECK(title);
    CHECK(!into_gap);
}

void test_nx_comp(void)
{
    struct nx_list l;
    struct nx_kev e;
    nx_anim_set_reduced(0);
    test_fit();
    test_title_target();

    /* 30 rows of 32 px, 5 visible from y 52 */
    clean();
    nx_list_init(&l, 52, NX_ROW_H, 5, 30, 0, 1000);
    CHECK_EQ(l.sel, 0);
    CHECK_EQ(l.top, 0);
    nx_rect v = nx_list_view(&l);
    CHECK(v.y0 == 52 && v.y1 == 212 && v.x0 == 0 && v.x1 == 320);
    nx_rect r = nx_list_row(&l, 2);
    CHECK(r.y0 == 116 && r.y1 == 148);

    /* DOWN inside the view: the highlight slides; both rows are dirty until it ends */
    clean();
    e = key(NXK_PRESS, NX_KEY_DOWN);
    CHECK_EQ(nx_list_key(&l, &e, 2000), 1);
    CHECK_EQ(l.sel, 1);
    CHECK_EQ(l.anim, NX_LA_SLIDE);
    CHECK(dirty_covers(nx_rect_or(nx_list_row(&l, 0), nx_list_row(&l, 1))));
    clean();
    nx_list_tick(&l, 2060);
    CHECK_EQ(l.anim, NX_LA_SLIDE);
    CHECK(dirty_covers(nx_list_row(&l, 1)));
    nx_list_tick(&l, 2000 + NX_SLIDE_MS);
    CHECK_EQ(l.anim, NX_LA_NONE);          /* this last invalidation draws the final frame */
    clean();
    nx_list_tick(&l, 2200);
    CHECK_EQ(nx_dirty_count(), 0);

    /* down to row 5: the view scrolls by one row (the whole view is dirty) */
    for (unsigned i = 0; i < 3u; i++) {
        e = key(NXK_PRESS, NX_KEY_DOWN);
        nx_list_key(&l, &e, 3000);
    }
    CHECK_EQ(l.sel, 4);
    CHECK_EQ(l.top, 0);
    clean();
    nx_list_key(&l, &e, 4000);
    CHECK_EQ(l.sel, 5);
    CHECK_EQ(l.top, 1);
    CHECK_EQ(l.anim, NX_LA_SCROLL);
    CHECK_EQ(l.from, 0);
    CHECK(dirty_covers(v));

    /* a held key (REPEAT) jumps without animation; the end stops it */
    e = key(NXK_REPEAT, NX_KEY_DOWN);
    for (unsigned i = 0; i < 40u; i++)
        nx_list_key(&l, &e, 5000 + i);
    CHECK_EQ(l.sel, 29);
    CHECK_EQ(l.top, 25);
    CHECK_EQ(l.anim, NX_LA_NONE);
    CHECK_EQ(nx_list_key(&l, &e, 6000), 0);
    e = key(NXK_PRESS, NX_KEY_UP);
    nx_list_key(&l, &e, 7000);
    CHECK_EQ(l.sel, 28);
    CHECK_EQ(l.top, 25);

    /* reduced motion: no slide either */
    nx_anim_set_reduced(1);
    nx_list_key(&l, &e, 8000);
    CHECK_EQ(l.anim, NX_LA_NONE);
    nx_anim_set_reduced(0);

    /* a jump keeps the selection in view, a short list never scrolls */
    nx_list_select(&l, 3, 9000);
    CHECK(l.top <= 3 && l.top + 5u > 3u);
    nx_list_init(&l, 52, NX_ROW_H, 5, 3, 9, 1000);
    CHECK_EQ(l.sel, 2);
    CHECK_EQ(l.top, 0);
    nx_list_init(&l, 52, NX_ROW_H, 5, 0, 0, 1000);
    e = key(NXK_PRESS, NX_KEY_DOWN);
    CHECK_EQ(nx_list_key(&l, &e, 1000), 0);

    /* marquee: offset 0 for a second, then 2 px steps (each one dirties the box) */
    nx_rect box = { 44, 60, 168, 80 };
    int16_t last = 0;
    clean();
    nx_marquee_tick(&last, 300, 124, 500, box);
    CHECK_EQ(last, 0);
    CHECK_EQ(nx_dirty_count(), 0);
    nx_marquee_tick(&last, 300, 124, 1100, box);
    CHECK(last > 0);
    CHECK(dirty_covers(box));
    clean();
    nx_marquee_tick(&last, 100, 124, 5000, box);   /* fits: no marquee */
    CHECK_EQ(last, 0);
}
