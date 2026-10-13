/* nx raster core (src/nx/nx_gfx.c) into host framebuffers: the RGB565 blend, clipping and the
 * probe mode, rounded shapes, bars, segments, rings, the dithered gradient, text, the marquee
 * clock, and the property the band renderer relies on: a scene drawn band by band (any band
 * height and width) is pixel for pixel the scene drawn in one piece. */
#include "harness.h"

#include "nx_font.h"
#include "nx_gfx.h"
#include "test_nx.h"

static uint16_t fb[240 * 320], ref[240 * 320];

static nx_gc gc_for(uint16_t *px, nx_rect band)
{
    nx_gc gc = { px, band, band, nx_palettes[NX_THEME_DARK], 0, 0, 0, 0 };
    return gc;
}

static nx_gc screen_gc(uint16_t *px)
{
    return gc_for(px, (nx_rect){ 0, 0, 320, 240 });
}

static uint32_t lcg = 12345u;
static uint32_t rnd(void)
{
    lcg = lcg * 1103515245u + 12345u;
    return lcg >> 8;
}

static uint16_t mix_ref(uint16_t bg, uint16_t fg, unsigned a)
{
    uint16_t out = 0;
    static const unsigned sh[3] = { 11, 5, 0 }, w[3] = { 5, 6, 5 };
    for (unsigned k = 0; k < 3; k++) {
        int b = (bg >> sh[k]) & ((1 << w[k]) - 1), f = (fg >> sh[k]) & ((1 << w[k]) - 1);
        int d = (f - b) * (int)a;
        int v = b + (d >= 0 ? d / 32 : -((-d + 31) / 32));   /* floor */
        out = (uint16_t)(out | v << sh[k]);
    }
    return out;
}

static void test_mix(void)
{
    int ok = 1;
    for (unsigned i = 0; i < 20000u; i++) {
        uint16_t b = (uint16_t)rnd(), f = (uint16_t)rnd();
        unsigned a = rnd() % 33u;
        if (nx_mix(b, f, a) != mix_ref(b, f, a))
            ok = 0;
    }
    CHECK(ok);
    CHECK_EQ(nx_mix(0x1234u, 0xFEDCu, 0), 0x1234u);
    CHECK_EQ(nx_mix(0x1234u, 0xFEDCu, 32), 0xFEDCu);
    CHECK_EQ(k_nx_a16[0], 0);
    CHECK_EQ(k_nx_a16[15], 32);
    nx_gc gc = screen_gc(fb);
    CHECK_EQ(nx_col(&gc, NX_PAL_TEXT1), nx_palettes[NX_THEME_DARK][NX_PAL_TEXT1]);
    gc.tint = 32;
    CHECK_EQ(nx_col(&gc, NX_PAL_TEXT1), nx_palettes[NX_THEME_DARK][NX_PAL_BG]);
}

static void test_clip_probe(void)
{
    memset(fb, 0, sizeof fb);
    nx_gc gc = screen_gc(fb);
    gc.clip = (nx_rect){ 10, 10, 20, 20 };
    nx_fill(&gc, (nx_rect){ 0, 0, 100, 100 }, 0xFFFFu);
    CHECK_EQ(fb[10 * 320 + 10], 0xFFFFu);
    CHECK_EQ(fb[19 * 320 + 19], 0xFFFFu);
    CHECK_EQ(fb[9 * 320 + 10], 0);
    CHECK_EQ(fb[10 * 320 + 20], 0);
    CHECK_EQ(gc.hits, 1);
    nx_fill(&gc, (nx_rect){ 30, 30, 40, 40 }, 0xFFFFu);   /* outside the clip: no hit */
    CHECK_EQ(gc.hits, 1);
    /* probe: hits are counted, nothing is written */
    nx_gc probe = gc_for(NULL, (nx_rect){ 0, 0, 320, 8 });
    nx_rrect(&probe, (nx_rect){ 0, 4, 50, 30 }, 6, 0xFFFFu);
    nx_text(&probe, &nx_font_body16, 0, 100, "below", 0xFFFFu);
    nx_ring(&probe, 160, 4, 10, 3, 0, 1024, 0, 0xFFFFu);
    CHECK_EQ(probe.hits, 2);
    /* the offset moves primitives and clips alike */
    memset(fb, 0, sizeof fb);
    nx_gc o = screen_gc(fb);
    o.ox = 5;
    o.oy = 7;
    nx_fill(&o, (nx_rect){ 0, 0, 2, 2 }, 0x1234u);
    CHECK_EQ(fb[7 * 320 + 5], 0x1234u);
    CHECK_EQ(fb[0], 0);
}

static unsigned count_color(const uint16_t *p, uint16_t c)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 320u * 240u; i++)
        n += p[i] == c;
    return n;
}

static void test_shapes(void)
{
    const uint16_t W = 0xFFFFu;
    /* rounded rectangle: full inside, blended corners, 4-fold symmetry, nothing outside */
    memset(fb, 0, sizeof fb);
    nx_gc gc = screen_gc(fb);
    nx_rrect(&gc, (nx_rect){ 100, 100, 140, 120 }, 8, W);
    CHECK_EQ(fb[110 * 320 + 120], W);
    CHECK_EQ(fb[100 * 320 + 120], W);
    CHECK(fb[100 * 320 + 100] != W);                   /* corner pixel only partly covered */
    CHECK_EQ(fb[99 * 320 + 120], 0);
    CHECK_EQ(fb[110 * 320 + 140], 0);
    int sym = 1;
    for (int y = 100; y < 120; y++)
        for (int x = 100; x < 140; x++)
            if (fb[y * 320 + x] != fb[y * 320 + (239 - x)] || fb[y * 320 + x] != fb[(219 - y) * 320 + x])
                sym = 0;
    CHECK(sym);
    /* outline: the inside stays untouched */
    memset(fb, 0, sizeof fb);
    nx_rrect_line(&gc, (nx_rect){ 10, 10, 60, 40 }, 8, 2, W);
    CHECK_EQ(fb[25 * 320 + 10], W);
    CHECK_EQ(fb[25 * 320 + 11], W);
    CHECK_EQ(fb[25 * 320 + 12], 0);
    CHECK_EQ(fb[25 * 320 + 35], 0);
    CHECK_EQ(fb[10 * 320 + 35], W);

    /* progress bar: the filled amount grows with every step of frac (sub-pixel end) */
    uint32_t prev = 0;
    int grows = 1;
    for (uint32_t f = 0; f <= 65535u; f += 1500u) {
        memset(fb, 0, sizeof fb);
        nx_bar(&gc, (nx_rect){ 20, 50, 220, 60 }, (uint16_t)f, 0x001Fu, 0x0000u);
        uint32_t sum = 0;
        for (int y = 50; y < 60; y++)
            for (int x = 20; x < 220; x++)
                sum += fb[y * 320 + x] & 31u;
        if (f && sum <= prev)
            grows = 0;
        prev = sum;
    }
    CHECK(grows);
    memset(fb, 0, sizeof fb);
    nx_bar(&gc, (nx_rect){ 20, 50, 220, 60 }, 0, 0x001Fu, 0x0800u);
    CHECK_EQ(count_color(fb, 0x001Fu), 0);
    CHECK_EQ(fb[55 * 320 + 120], 0x0800u);
    nx_bar(&gc, (nx_rect){ 20, 50, 220, 60 }, 65535u, 0x001Fu, 0x0800u);
    CHECK_EQ(count_color(fb, 0x0800u), 0);

    /* ring: annulus only, quarter arc only in its quadrant */
    memset(fb, 0, sizeof fb);
    nx_ring(&gc, 160, 120, 20, 4, 0, 1024, 0, W);
    CHECK_EQ(fb[120 * 320 + 178], W);                 /* 18.5 px from the centre */
    CHECK_EQ(fb[120 * 320 + 160], 0);
    CHECK_EQ(fb[120 * 320 + 185], 0);
    memset(fb, 0, sizeof fb);
    nx_ring(&gc, 160, 120, 20, 4, 0, 256, 0, W);      /* 12 to 3 o'clock */
    CHECK_EQ(fb[106 * 320 + 173], W);                 /* top right */
    CHECK_EQ(fb[133 * 320 + 146], 0);                 /* bottom left */
    unsigned plain = count_color(fb, W);
    memset(fb, 0, sizeof fb);
    nx_ring(&gc, 160, 120, 20, 4, 0, 256, 1, W);      /* round caps add a little */
    CHECK(count_color(fb, W) > plain);

    /* sine table */
    CHECK_EQ(nx_sin1024(0), 0);
    CHECK_EQ(nx_sin1024(256), 16384);
    CHECK_EQ(nx_sin1024(512), 0);
    CHECK_EQ(nx_sin1024(768), -16384);
    int unit = 1;
    for (uint32_t a = 0; a < 1024u; a++) {
        int32_t s = nx_sin1024(a), c = nx_cos1024(a);
        int32_t m = (s * s + c * c) >> 14;
        if (m < 16384 - 40 || m > 16384 + 40)
            unit = 0;
    }
    CHECK(unit);

    /* segments: proportional widths, 2 px gaps, the whole width, minimum 4 px */
    int16_t x0[4], x1[4];
    static const uint16_t d1[3] = { 1, 1, 2 };
    nx_segbar_layout(296, d1, 3, x0, x1);
    CHECK_EQ(x0[0], 0);
    CHECK_EQ(x1[2], 296);
    CHECK_EQ(x0[1], x1[0] + 2);
    CHECK((x1[2] - x0[2]) > (x1[0] - x0[0]) * 3 / 2);
    static const uint16_t d2[3] = { 60000, 1, 1 };
    nx_segbar_layout(100, d2, 3, x0, x1);
    CHECK(x1[1] - x0[1] >= 4 && x1[2] - x0[2] >= 4);
    CHECK_EQ(x1[2], 100);
    nx_segbar_layout(10, d1, 3, x0, x1);             /* too narrow: equal widths */
    CHECK_EQ(x1[2], 10);
}

static void test_gradient(void)
{
    memset(fb, 0, sizeof fb);
    nx_gc gc = screen_gc(fb);
    nx_vgrad(&gc, (nx_rect){ 0, 0, 64, 64 }, 0x0000u, 0xFFFFu);
    uint32_t prev = 0;
    int rising = 1, close = 1;
    for (int y = 0; y < 64; y++) {
        uint32_t sum = 0;
        for (int x = 0; x < 64; x++) {
            uint16_t v = fb[y * 320 + x];
            sum += (v >> 5) & 63u;
            int ideal = y * 63 / 63;   /* green level y of 0..63 */
            int g = (v >> 5) & 63;
            if (g < ideal - 1 || g > ideal + 1)
                close = 0;
        }
        if (y && sum < prev)
            rising = 0;
        prev = sum;
    }
    CHECK(rising);
    CHECK(close);
    CHECK_EQ(fb[0], 0);
    CHECK_EQ(fb[63 * 320 + 63], 0xFFFFu);
}

static void test_text(void)
{
    memset(fb, 0, sizeof fb);
    nx_gc gc = screen_gc(fb);
    const nx_font_t *f = &nx_font_body16;
    int16_t end = nx_text(&gc, f, 10, 10, "Hello", 0xFFFFu);
    CHECK_EQ(end, 10 + (int)nx_font_text_width(f, "Hello"));
    unsigned n = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            if (fb[y * 320 + x]) {
                n++;
                CHECK(x >= 10 && x < end + 2 && y >= 10 && y < 10 + f->height);
            }
    CHECK(n > 50);
    CHECK(count_color(fb, 0xFFFFu) > 10);              /* opaque glyph pixels */
    /* cut to 40 px with the ellipsis */
    memset(fb, 0, sizeof fb);
    end = nx_text_fit(&gc, f, 0, 0, 40, "A rather long name", 0xFFFFu);
    CHECK(end <= 40);
    /* tabular cells */
    int16_t cx[8];
    CHECK_EQ(nx_text_cells(&nx_font_num48, "12.5", 3, cx, 7), 4);
    CHECK_EQ(cx[0], 3);
    CHECK_EQ(cx[1] - cx[0], cx[2] - cx[1]);
    CHECK_EQ(cx[4], 3 + (int)nx_font_text_width(&nx_font_num48, "12.5"));
    /* icons: inside their 20 px box */
    memset(fb, 0, sizeof fb);
    nx_icon(&gc, &nx_icons20, NX_I20_PLAY, 40, 40, 0xFFFFu);
    int inside = 1;
    n = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            if (fb[y * 320 + x]) {
                n++;
                if (x < 40 || x >= 60 || y < 40 || y >= 60)
                    inside = 0;
            }
    CHECK(inside && n > 20);
    /* marquee clock */
    CHECK_EQ(nx_marquee_offset(100, 120, 5000), 0);
    CHECK_EQ(nx_marquee_offset(200, 120, 999), 0);
    CHECK_EQ(nx_marquee_offset(200, 120, 1050), 2);
    CHECK_EQ(nx_marquee_offset(200, 120, 1099), 2);
    CHECK_EQ(nx_marquee_offset(200, 120, 1100), 4);
    uint32_t period = 1000u + (200u + NX_MARQUEE_GAP) * 25u;
    CHECK_EQ(nx_marquee_offset(200, 120, period + 1050), 2);
    int bounded = 1;
    for (uint32_t t = 0; t < 2 * period; t += 7)
        if (nx_marquee_offset(200, 120, t) >= 200 + NX_MARQUEE_GAP)
            bounded = 0;
    CHECK(bounded);
}

/* flat trace: a line 2 * half width thick around y */
static int32_t flat_y(void *ctx, int16_t i)
{
    (void)i;
    return *(const int32_t *)ctx;
}

static int32_t ramp_y(void *ctx, int16_t i)
{
    (void)ctx;
    return (int32_t)(20 + i / 2) * 16;
}

static void scene(nx_gc *gc)
{
    const uint16_t *pal = gc->pal;
    nx_vgrad(gc, (nx_rect){ 0, 0, 320, 240 }, pal[NX_PAL_BG_TOP], pal[NX_PAL_BG]);
    nx_rrect(gc, (nx_rect){ 12, 30, 150, 110 }, 12, pal[NX_PAL_SURFACE1]);
    nx_rrect_line(gc, (nx_rect){ 160, 30, 308, 110 }, 12, 2, pal[NX_PAL_OUTLINE]);
    nx_pill(gc, (nx_rect){ 20, 120, 90, 140 }, pal[NX_PAL_G1]);
    nx_text(gc, &nx_font_cap13, 30, 121, "G1", pal[NX_PAL_ON_ACCENT]);
    nx_bar(gc, (nx_rect){ 12, 150, 308, 158 }, 40000u, pal[NX_PAL_G2], pal[NX_PAL_TRACK]);
    static const uint16_t dur[4] = { 3, 1, 2, 2 };
    nx_segbar(gc, (nx_rect){ 12, 162, 308, 170 }, dur, 4, 1, 30000u, pal[NX_PAL_G1], pal[NX_PAL_G1],
              pal[NX_PAL_TRACK]);
    nx_ring(gc, 240, 70, 30, 6, 100, 700, 1, pal[NX_PAL_G1]);
    nx_disc(gc, 240 * 16 + 5, 70 * 16 + 3, 5 * 16 + 8, pal[NX_PAL_WARNING]);
    nx_text(gc, &nx_font_num48, 20, 40, "727.0", pal[NX_PAL_TEXT1]);
    nx_text(gc, &nx_font_body16, 20, 180, "Lyme Borreliosis", pal[NX_PAL_TEXT1]);
    nx_text_fit(gc, &nx_font_body16, 20, 210, 120, "A very long program name", pal[NX_PAL_TEXT2]);
    nx_icon(gc, &nx_icons20, NX_I20_REPEAT, 280, 180, pal[NX_PAL_G2]);
    nx_icon(gc, &nx_icons32, NX_I32_CHECK_CIRCLE, 240, 200, pal[NX_PAL_SUCCESS]);
    nx_trace(gc, 160, 140, ramp_y, NULL, 16, pal[NX_PAL_G2], 32);
    nx_text_marquee(gc, &nx_font_body16, (nx_rect){ 170, 120, 300, 140 }, "Scrolling text that is long", 37,
                    pal[NX_PAL_TEXT1]);
}

static void test_bands_equal_whole(void)
{
    nx_gc whole = screen_gc(ref);
    scene(&whole);
    static const int16_t heights[] = { 1, 7, 8, 13, 240 };
    static const int16_t widths[] = { 320, 100, 37 };
    int same_all = 1;
    for (unsigned hi = 0; hi < sizeof heights / sizeof heights[0]; hi++) {
        for (unsigned wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
            static uint16_t band[320 * 240];
            memset(fb, 0, sizeof fb);
            int16_t bw = widths[wi], bh = heights[hi];
            for (int16_t y = 0; y < 240; y = (int16_t)(y + bh)) {
                for (int16_t x = 0; x < 320; x = (int16_t)(x + bw)) {
                    nx_rect r = { x, y, (int16_t)(x + bw < 320 ? x + bw : 320), (int16_t)(y + bh < 240 ? y + bh : 240) };
                    nx_gc gc = gc_for(band, r);
                    scene(&gc);
                    int w = r.x1 - r.x0;
                    for (int yy = r.y0; yy < r.y1; yy++)
                        memcpy(&fb[yy * 320 + r.x0], &band[(yy - r.y0) * w], (size_t)w * 2u);
                }
            }
            if (memcmp(fb, ref, sizeof fb) != 0) {
                same_all = 0;
                printf("bands %dx%d differ from the whole scene\n", bw, bh);
            }
        }
    }
    CHECK(same_all);

    /* a flat trace is 2 * half width thick */
    memset(fb, 0, sizeof fb);
    nx_gc gc = screen_gc(fb);
    int32_t y = 100 * 16;
    nx_trace(&gc, 10, 50, flat_y, &y, 16, 0xFFFFu, 32);
    CHECK_EQ(fb[99 * 320 + 20], 0xFFFFu);
    CHECK_EQ(fb[100 * 320 + 20], 0xFFFFu);
    CHECK_EQ(fb[98 * 320 + 20], 0);
    CHECK_EQ(fb[101 * 320 + 20], 0);
}

/* Every glyph and icon drawn straight from the tables equals nx_glyph_decode (the format's
 * reference decoder) blended by hand: whole, clipped on all four sides, and in 3-row bands
 * starting at every row (RLE runs that cross a band's top edge). */
static void test_glyphs_match_decoder(void)
{
    const nx_font_t *sets[] = { &nx_font_cap13, &nx_font_body16, &nx_font_num28, &nx_font_num48, &nx_icons20,
                                &nx_icons32 };
    static uint8_t alpha[64 * 64];
    const uint16_t bg = 0x2104u, c = 0xFD20u;
    unsigned glyphs = 0;
    int ok = 1;
    for (unsigned s = 0; s < sizeof sets / sizeof sets[0]; s++) {
        const nx_font_t *f = sets[s];
        unsigned dense = f->first <= f->last ? (unsigned)(f->last - f->first) + 1u : 0u;
        for (unsigned i = 0; i < dense + f->n_extra; i++) {
            uint8_t code = i < dense ? (uint8_t)(f->first + i) : f->extra[i - dense];
            const nx_glyph_t *g = nx_font_glyph(f, code);
            if (!g->w)
                continue;
            glyphs++;
            nx_glyph_decode(f, g, 0, g->h, alpha);
            int gx = 37 + g->x_off, gy = 21 + g->y_off;
            for (int clipped = -1; clipped < g->h; clipped++) {
                nx_rect band = { 0, 0, 320, 240 };
                if (clipped == 0)
                    band = (nx_rect){ (int16_t)(gx + 1), (int16_t)(gy + 2), (int16_t)(gx + g->w - 1),
                                      (int16_t)(gy + g->h - 1) };
                if (clipped > 0)
                    band = (nx_rect){ 0, (int16_t)(gy + clipped), 320, (int16_t)(gy + clipped + 3) };
                static uint16_t buf[320 * 240];
                int bw = band.x1 - band.x0;
                for (int k = 0; k < bw * (band.y1 - band.y0); k++)
                    buf[k] = bg;
                nx_gc gc = gc_for(buf, band);
                if (f->height == f->baseline && f->first == 0)
                    nx_icon(&gc, f, code, 37, 21, c);
                else
                    nx_text_n(&gc, f, 37, 21, (const char *)&code, 1, c);
                for (int y = band.y0; y < band.y1; y++)
                    for (int x = band.x0; x < band.x1; x++) {
                        uint16_t want = bg;
                        if (x >= gx && x < gx + g->w && y >= gy && y < gy + g->h) {
                            uint8_t a = alpha[(y - gy) * g->w + (x - gx)];
                            want = a == 15 ? c : a ? nx_mix(bg, c, k_nx_a16[a]) : bg;
                        }
                        if (buf[(y - band.y0) * bw + (x - band.x0)] != want)
                            ok = 0;
                    }
            }
        }
    }
    CHECK(ok);
    CHECK(glyphs > 200);   /* 241 glyphs with ink in the English-only tables */
}

void test_nx_gfx(void)
{
    test_glyphs_match_decoder();
    test_mix();
    test_clip_probe();
    test_shapes();
    test_gradient();
    test_text();
    test_bands_equal_whole();
}
