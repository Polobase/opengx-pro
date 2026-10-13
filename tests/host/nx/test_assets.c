/* Host tests of the nx asset format (src/nx/nx_font.c) against the goldens that
 * tools/gen_nx_assets.py writes next to the tables (nx_asset_vectors.h): glyph lookup, full and
 * row-band decoding, text width, ellipsis fitting, the English strings, palettes. Pure
 * module: no mocks. */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "nx/nx_font.h"
#include "nx_asset_vectors.h"

int g_checks, g_fails;

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void test_glyphs(void)
{
    static uint8_t buf[64 * 64 + 1];   /* more than the largest glyph (32 px icons) + a guard byte */
    for (size_t i = 0; i < N(k_vec_glyphs); i++) {
        const vec_glyph_t *v = &k_vec_glyphs[i];
        const nx_glyph_t *g = nx_font_glyph(v->font, v->code);
        CHECK(g != NULL);
        if (!g)
            continue;
        CHECK_EQ(g->w, v->w);
        CHECK_EQ(g->h, v->h);
        CHECK_EQ(g->x_off, v->x_off);
        CHECK_EQ(g->y_off, v->y_off);
        CHECK_EQ(g->advance, v->advance);
        CHECK_EQ(!!(g->off & NX_GLYPH_RLE), v->rle);
        size_t n = (size_t)g->w * g->h;
        CHECK(n < sizeof(buf));
        /* whole glyph */
        memset(buf, 0xEE, sizeof(buf));
        nx_glyph_decode(v->font, g, 0, g->h, buf);
        if (memcmp(buf, v->alpha, n) != 0)
            printf("%s: full decode differs\n", v->what);
        CHECK(memcmp(buf, v->alpha, n) == 0);
        CHECK_EQ(buf[n], 0xEE);   /* nothing written past w*h */
        /* every band y0..y1 must equal the same rows of the full glyph */
        int bands_ok = 1;
        for (unsigned y0 = 0; y0 <= g->h; y0++) {
            for (unsigned y1 = y0; y1 <= g->h + 1u; y1++) {
                memset(buf, 0xEE, sizeof(buf));
                nx_glyph_decode(v->font, g, y0, y1, buf);
                unsigned last = y1 > g->h ? g->h : y1;
                size_t rows = last > y0 ? last - y0 : 0;
                if (memcmp(buf, v->alpha + (size_t)y0 * g->w, rows * g->w) != 0 || buf[rows * g->w] != 0xEE)
                    bands_ok = 0;
            }
        }
        if (!bands_ok)
            printf("%s: band decode differs\n", v->what);
        CHECK(bands_ok);
    }
}

static void test_lookup(void)
{
    /* dense range, sorted extras, misses */
    CHECK(nx_font_glyph(&nx_font_body16, ' ') == &nx_font_body16.glyphs[0]);
    CHECK(nx_font_glyph(&nx_font_body16, '~') == &nx_font_body16.glyphs['~' - ' ']);
    CHECK(nx_font_glyph(&nx_font_body16, NX_CH_middot) == &nx_font_body16.glyphs[95]);
    CHECK(nx_font_glyph(&nx_font_body16, NX_CH_rsaquo) != NULL);
    CHECK(nx_font_glyph(&nx_font_body16, 0x7F) == NULL);
    CHECK(nx_font_glyph(&nx_font_body16, 0x01) == NULL);
    CHECK(nx_font_glyph(&nx_font_body16, 0xFF) == NULL);
    /* each text font carries only the extras the UI draws with it, and no German letters */
    CHECK(nx_font_glyph(&nx_font_cap13, NX_CH_rtri) != NULL);
    CHECK(nx_font_glyph(&nx_font_body16, NX_CH_rtri) == NULL);
    CHECK(nx_font_glyph(&nx_font_body16, NX_CH_auml) == NULL);
    CHECK(nx_font_glyph(&nx_font_cap13, NX_CH_szlig) == NULL);
    CHECK(nx_font_glyph(&nx_font_num48, 'A') == NULL);
    CHECK(nx_font_glyph(&nx_font_num48, '0') == &nx_font_num48.glyphs[0]);
    CHECK(nx_font_glyph(&nx_font_num48, ' ') == &nx_font_num48.glyphs[10]);   /* first extra */
    CHECK(nx_font_glyph_or_fallback(&nx_font_body16, 0x7F) == nx_font_glyph(&nx_font_body16, '?'));
    CHECK(nx_font_glyph_or_fallback(&nx_font_num48, 'A') == NULL);
    /* the brand font holds the wordmark's letters only (the splash draws its "O" as a ring) */
    CHECK(nx_font_glyph(&nx_font_brand, 'p') == &nx_font_brand.glyphs['p' - 'n']);
    CHECK(nx_font_glyph(&nx_font_brand, 'G') != NULL);
    CHECK(nx_font_glyph(&nx_font_brand, 'O') == NULL);
    CHECK(nx_font_glyph(&nx_font_brand, '0') == NULL);
    /* every extra code of every font resolves to its own record */
    const nx_font_t *fonts[] = { &nx_font_cap13, &nx_font_body16, &nx_font_num28, &nx_font_num48, &nx_font_brand };
    for (size_t i = 0; i < N(fonts); i++) {
        const nx_font_t *f = fonts[i];
        unsigned dense = (unsigned)(f->last - f->first) + 1u;
        for (unsigned e = 0; e < f->n_extra; e++)
            CHECK(nx_font_glyph(f, f->extra[e]) == &f->glyphs[dense + e]);
    }
    /* icon sets: codes are indices */
    CHECK(nx_font_glyph(&nx_icons20, NX_I20_PLAY) == &nx_icons20.glyphs[0]);
    CHECK(nx_font_glyph(&nx_icons20, NX_I20_COUNT) == NULL);
    CHECK(nx_font_glyph(&nx_icons32, NX_I32_COUNT - 1) != NULL);
    CHECK_EQ(nx_icons20.height, 20);
    CHECK_EQ(nx_icons32.height, 32);
}

static void test_measure(void)
{
    for (size_t i = 0; i < N(k_vec_widths); i++)
        CHECK_EQ(nx_font_text_width(k_vec_widths[i].font, k_vec_widths[i].text), k_vec_widths[i].width);
    for (size_t i = 0; i < N(k_vec_fits); i++) {
        const vec_fit_t *v = &k_vec_fits[i];
        nx_fit_t r = nx_font_fit(v->font, v->text, v->max_px);
        CHECK_EQ(r.n, v->n);
        CHECK_EQ(r.width, v->width);
        CHECK_EQ(r.ellipsis, v->ellipsis);
        CHECK(r.width <= v->max_px);
    }
    CHECK_EQ(nx_font_text_width(&nx_font_body16, ""), 0);
    CHECK_EQ(nx_font_text_width_n(&nx_font_body16, "AAAA", 2), 2u * nx_font_glyph(&nx_font_body16, 'A')->advance);
    /* tabular digits: all digits advance alike in every font */
    const nx_font_t *fonts[] = { &nx_font_cap13, &nx_font_body16, &nx_font_num28, &nx_font_num48 };
    for (size_t i = 0; i < N(fonts); i++)
        for (char c = '1'; c <= '9'; c++)
            CHECK_EQ(nx_font_glyph(fonts[i], (uint8_t)c)->advance, nx_font_glyph(fonts[i], '0')->advance);
    /* a font's own ellipsis glyph wins over "...": no table carries one, so a two-glyph font */
    static const nx_glyph_t k_ell_glyphs[2] = { { 0, 0, 0, 0, 0, 3, 0 }, { 0, 0, 0, 0, 0, 9, 0 } };
    static const uint8_t k_ell_extra[1] = { NX_CH_hellip };
    static const nx_font_t k_ell = { 10, 8, 4, '.', '.', 1, { 0, 0 }, k_ell_extra, k_ell_glyphs, NULL };
    CHECK_EQ((uint8_t)nx_font_ellipsis(&k_ell)[0], NX_CH_hellip);
    CHECK_EQ((uint8_t)nx_font_ellipsis(&nx_font_body16)[0], NX_CH_hellip);   /* the text fonts carry "…" */
    CHECK_EQ((uint8_t)nx_font_ellipsis(&nx_font_cap13)[0], NX_CH_hellip);
    CHECK(strcmp(nx_font_ellipsis(&nx_font_num48), "...") == 0);
    CHECK(strcmp(nx_font_ellipsis(&nx_icons32), "") == 0);   /* icon indices only */
}

static void test_strings(void)
{
    CHECK_EQ(NX_LANG_COUNT, 1);   /* English only */
    for (size_t i = 0; i < N(k_vec_strs); i++) {
        CHECK_EQ(k_vec_strs[i].lang, NX_LANG_EN);
        CHECK(strcmp(nx_str(k_vec_strs[i].id), k_vec_strs[i].text) == 0);
    }
    CHECK(strcmp(nx_str(NX_S_STATE_READY), "READY") == 0);
    CHECK(strcmp(nx_str(NX_STR_COUNT), "") == 0);
    /* every string is non-empty and fits the pool */
    for (unsigned id = 0; id < NX_STR_COUNT; id++)
        CHECK(nx_str(id)[0] != 0);
    char buf[64];
    size_t n = nx_str_format(buf, sizeof(buf), VEC_FMT_ID, k_vec_fmt_args, 3);
    CHECK(strcmp(buf, VEC_FMT_EXPECTED) == 0);
    CHECK_EQ(n, strlen(VEC_FMT_EXPECTED));
    n = nx_str_format(buf, 4, VEC_FMT_ID, k_vec_fmt_args, 3);   /* truncated, still terminated */
    CHECK_EQ(n, 3);
    CHECK(strncmp(buf, VEC_FMT_EXPECTED, 3) == 0 && buf[3] == 0);
    n = nx_str_format(buf, sizeof(buf), VEC_FMT_ID, NULL, 0);   /* no args: slots vanish */
    CHECK(strchr(buf, 1) == NULL && strchr(buf, 2) == NULL && n == strlen(buf));
    CHECK_EQ(nx_str_format(buf, 0, VEC_FMT_ID, NULL, 0), 0);
}

static void test_palettes(void)
{
    for (size_t i = 0; i < N(k_vec_pals); i++)
        CHECK_EQ(nx_palettes[k_vec_pals[i].theme][k_vec_pals[i].token], k_vec_pals[i].rgb565);
    CHECK_EQ(nx_palettes[NX_THEME_DARK][NX_PAL_BG], 0x0882);   /* #0B0F14: r 1, g 4, b 2 */
}

/* The support page's QR code (assets/nx/qr.json): version 3, so 29 modules, with the three
 * finder patterns, the timing lines and the dark module where the standard puts them, and as
 * many dark modules as the tool counted. That it reads as the address is checked on the rendered
 * screen (site/test, tests/gallery/nx/support.png). */
static int qr_dark(unsigned row, unsigned col)
{
    return nx_qr_support[row * ((NX_QR_SUPPORT_N + 7u) / 8u) + (col >> 3)] >> (7u - (col & 7u)) & 1u;
}

static void test_qr(void)
{
    const unsigned n = NX_QR_SUPPORT_N;
    CHECK_EQ(n, 29);
    CHECK_EQ(sizeof nx_qr_support, 29u * 4u);
    static const uint8_t k_finder[7] = { 0x7F, 0x41, 0x5D, 0x5D, 0x5D, 0x41, 0x7F };   /* rows, bit i = column i */
    static const uint8_t k_at[3][2] = { { 0, 0 }, { 0, 22 }, { 22, 0 } };              /* row, column */
    for (unsigned f = 0; f < 3u; f++)
        for (unsigned r = 0; r < 7u; r++)
            for (unsigned c = 0; c < 7u; c++)
                CHECK_EQ(qr_dark(k_at[f][0] + r, k_at[f][1] + c), k_finder[r] >> c & 1);
    for (unsigned i = 8; i < n - 8u; i++) {   /* timing: row 6 and column 6 alternate, dark on even */
        CHECK_EQ(qr_dark(6, i), !(i & 1u));
        CHECK_EQ(qr_dark(i, 6), !(i & 1u));
    }
    CHECK(qr_dark(n - 8u, 8));                /* the dark module */
    unsigned dark = 0, pad = 0;
    for (unsigned r = 0; r < n; r++) {
        for (unsigned c = 0; c < n; c++)
            dark += (unsigned)qr_dark(r, c);
        for (unsigned c = n; c < 32u; c++)    /* the unused bits of each row are 0 */
            pad += (unsigned)qr_dark(r, c);
    }
    CHECK_EQ(dark, VEC_QR_SUPPORT_DARK);
    CHECK_EQ(pad, 0);
}

int main(void)
{
    test_lookup();
    test_glyphs();
    test_measure();
    test_strings();
    test_palettes();
    test_qr();
    printf("test_nx_assets: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
