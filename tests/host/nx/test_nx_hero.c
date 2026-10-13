/* Hero frequency layout (src/nx/nx_hero.c): the w24 decode with clamping, the fixture set of
 * docs/nx/design/ui.md §2.8 with the real font advances (D1 digits are 31 px, the design table
 * assumed about 29), maximality of the choice, ghost zeros and the drawn extent. */
#include "harness.h"

#include "nx_font.h"
#include "nx_hero.h"
#include "test_nx.h"

static uint64_t enc(uint64_t m, unsigned e)   /* the w24 encoding: Hz = m * 10^(e - 8) */
{
    return m * 10u + e;
}

static void test_decode(void)
{
    uint32_t ip, fr;
    uint8_t over;
    nx_hero_decode(enc(727, 8), &ip, &fr, &over);
    CHECK(ip == 727 && fr == 0 && !over);
    nx_hero_decode(3750007u, &ip, &fr, &over);        /* stored programs: 37 500.0 Hz */
    CHECK(ip == 37500 && fr == 0 && !over);
    nx_hero_decode(enc(12345678, 6), &ip, &fr, &over);
    CHECK(ip == 123456 && fr == 78000000u && !over);
    nx_hero_decode(enc(1, 0), &ip, &fr, &over);       /* 1e-8 Hz */
    CHECK(ip == 0 && fr == 1 && !over);
    nx_hero_decode(enc(99999999ull, 8), &ip, &fr, &over);
    CHECK(ip == 99999999u && fr == 0 && !over);
    nx_hero_decode(enc(1000000000ull, 9), &ip, &fr, &over);   /* 10^10 Hz: clamped, not wrapped */
    CHECK(ip == 99999999u && fr == 99999999u && over);
    nx_hero_decode(enc(10000000000000000ull, 0), &ip, &fr, &over);
    CHECK(over);
}

static void test_fixtures(void)
{
    nx_hero h;
    /* 727.00000000 Hz: 727.000 + tail 000 00 + Hz, all tail digits ghost zeros */
    CHECK(nx_hero_layout(enc(727, 8), &h));
    CHECK(h.font == NX_HERO_FIRST && h.k == 3);
    CHECK(strcmp(h.big, "727.000") == 0 && strcmp(h.tail, "00000") == 0);
    CHECK_EQ(h.tail_gaps, 1u << 3);                   /* "000 00" */
    CHECK_EQ(h.ghost, 0);
    CHECK(h.w <= NX_HERO_W);
#if NX_FEAT_HERO48
    /* 37 500 Hz: D1 with one decimal (the design's table assumed 29 px digits and got two) */
    CHECK(nx_hero_layout(3750007u, &h));
    CHECK(h.font == NX_HERO_D1 && h.k == 1);
    CHECK(strcmp(h.big, "37500.0") == 0 && strcmp(h.tail, "0000000") == 0);
    CHECK_EQ(h.big_gaps, 1u << 2);                    /* "37 500.0" */
    CHECK_EQ(h.tail_gaps, (1u << 2) | (1u << 5));     /* "00 000 00" */
    /* 123 456.78 Hz: 123 456 + tail .780 000 00 */
    CHECK(nx_hero_layout(enc(12345678, 6), &h));
    CHECK(h.font == NX_HERO_D1 && h.k == 0);
    CHECK(strcmp(h.big, "123456") == 0 && strcmp(h.tail, ".78000000") == 0);
    CHECK_EQ(h.big_gaps, 1u << 3);
    CHECK_EQ(h.tail_gaps, (1u << 4) | (1u << 7));     /* ".780 000 00" */
    CHECK_EQ(h.ghost, 3);                             /* ghost zeros after the 8 */
#else
    /* the 28 px digits alone (the image's default): three decimals for both */
    CHECK(nx_hero_layout(3750007u, &h));
    CHECK(h.font == NX_HERO_D2 && h.k == 3);
    CHECK(strcmp(h.big, "37500.000") == 0 && strcmp(h.tail, "00000") == 0);
    CHECK_EQ(h.big_gaps, 1u << 2);                    /* "37 500.000" */
    CHECK(nx_hero_layout(enc(12345678, 6), &h));
    CHECK(h.font == NX_HERO_D2 && h.k == 3);
    CHECK(strcmp(h.big, "123456.780") == 0 && strcmp(h.tail, "00000") == 0);
    CHECK_EQ(h.big_gaps, 1u << 3);
    CHECK_EQ(h.tail_gaps, 1u << 3);                   /* "000 00" */
    CHECK_EQ(h.ghost, 0);
#endif
    /* 12 345 678.12 Hz: D2 */
    CHECK(nx_hero_layout(enc(1234567812, 6), &h));
    CHECK(h.font == NX_HERO_D2);
    CHECK_EQ(h.k, 2);
    CHECK(strcmp(h.big, "12345678.12") == 0);
    CHECK_EQ(h.big_gaps, (1u << 2) | (1u << 5));      /* "12 345 678.12" */
    CHECK(h.w <= NX_HERO_W);
    /* overflow clamps */
    CHECK(nx_hero_layout(enc(1000000000ull, 9), &h));
    CHECK(h.over && strcmp(h.big, "99999999.99") == 0);

    /* every value fits, and every candidate before the chosen one (the first font k=3..0, then
     * D2 k=8..0) is too wide */
    static const uint64_t k_v[] = { 7278u, 3750007u, 123456786u, 12345678126u, 1u, 10u, 1000000007u, 123458u,
                                    9999999998u, 55555555555u, 43210u };
    int all = 1;
    for (unsigned i = 0; i < sizeof k_v / sizeof k_v[0]; i++) {
        nx_hero_layout(k_v[i], &h);
        if (h.w > NX_HERO_W || nx_hero_width(k_v[i], h.font, h.k) != h.w)
            all = 0;
        for (uint8_t font = NX_HERO_FIRST; font <= h.font; font++)
            for (int k = font == NX_HERO_FIRST ? 3 : 8; k >= 0; k--) {
                if (font == h.font && k == h.k)
                    break;
                if (nx_hero_width(k_v[i], font, (uint8_t)k) <= NX_HERO_W)
                    all = 0;
            }
    }
    CHECK(all);
}

static void test_draw(void)
{
    static uint16_t fb[240 * 320];
    memset(fb, 0, sizeof fb);
    nx_gc gc = { fb, { 0, 0, 320, 240 }, { 0, 0, 320, 240 }, nx_palettes[0], 0, 0, 0, 0 };
    nx_hero h;
    nx_hero_layout(enc(727, 8), &h);
    nx_hero_draw(&gc, &h, 12, 52, 0xFFFFu, 0x4208u, 0x8410u);
    int x_max = 0, y_min = 240, y_max = 0, ghosts = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t v = fb[y * 320 + x];
            if (!v)
                continue;
            if (x > x_max)
                x_max = x;
            if (y < y_min)
                y_min = y;
            if (y > y_max)
                y_max = y;
            ghosts += v == 0x4208u;
        }
    CHECK(x_max < 12 + h.w);
    CHECK(y_min >= 52);
    CHECK(y_max < 52 + nx_hero_height(&h));
    CHECK(ghosts > 20);                               /* the tail zeros in the ghost colour */
}

void test_nx_hero(void)
{
    test_decode();
    test_fixtures();
    test_draw();
}
