/* nx motion (src/nx/nx_anim.c): easing curves, tweens, reduced motion, the odometer roll with its
 * stagger, tint and live rule, and its drawing at the start and the end of a roll. */
#include "harness.h"

#include "nx_anim.h"
#include "nx_font.h"
#include "test_nx.h"

static void test_ease(void)
{
    static const uint8_t k_e[4] = { NX_EASE_LINEAR, NX_EASE_OUT, NX_EASE_IN, NX_EASE_IN_OUT };
    for (unsigned i = 0; i < 4u; i++) {
        CHECK_EQ(nx_ease(k_e[i], 0), 0);
        CHECK(nx_ease(k_e[i], 65535u) >= 65534u);
        int mono = 1;
        uint16_t prev = 0;
        for (uint32_t p = 0; p <= 65535u; p += 257u) {
            uint16_t v = nx_ease(k_e[i], (uint16_t)p);
            if (v < prev)
                mono = 0;
            prev = v;
        }
        CHECK(mono);
    }
    CHECK(nx_ease(NX_EASE_OUT, 32768u) > 50000u);    /* 1 - 0.5^3 = 0.875 */
    CHECK(nx_ease(NX_EASE_IN, 32768u) < 10000u);     /* 0.125 */
    uint16_t mid = nx_ease(NX_EASE_IN_OUT, 32768u);
    CHECK(mid > 32000u && mid < 33500u);

    nx_anim_set_reduced(0);
    CHECK_EQ(nx_anim_p(100, 100, 200, NX_EASE_LINEAR), 0);
    CHECK_EQ(nx_anim_p(100, 200, 200, NX_EASE_LINEAR), 32767);
    CHECK_EQ(nx_anim_p(100, 300, 200, NX_EASE_LINEAR), 65535);
    CHECK_EQ(nx_anim_p(100, 5000, 200, NX_EASE_OUT), 65535);   /* dropped frames skip ahead */
    nx_tween tw;
    nx_tween_start(&tw, -30, 0, 200, NX_EASE_OUT, 1000);
    CHECK_EQ(nx_tween_at(&tw, 1000), -30);
    int16_t m = nx_tween_at(&tw, 1100);
    CHECK(m > -10 && m < 0);
    CHECK_EQ(nx_tween_at(&tw, 1200), 0);
    CHECK(nx_tween_running(&tw, 1199));
    CHECK(!nx_tween_running(&tw, 1200));
    nx_anim_set_reduced(1);
    CHECK_EQ(nx_tween_at(&tw, 1000), 0);              /* reduced motion: at the end at once */
    CHECK_EQ(nx_anim_p(100, 100, 200, NX_EASE_OUT), 65535);
    nx_anim_set_reduced(0);
}

static void test_roll(void)
{
    nx_anim_set_reduced(0);
    nx_roll r;
    nx_roll_init(&r, "727.000");
    CHECK(!nx_roll_active(&r, 0));
    CHECK_EQ(nx_roll_set(&r, "727.000", 1000), 0);   /* unchanged */
    CHECK_EQ(nx_roll_set(&r, "728.000", 1000), 1);
    CHECK_EQ(r.delay[2], 0);
    CHECK_EQ(r.delay[0], 0xFF);
    CHECK_EQ(nx_roll_cell_p(&r, 0, 1000), 65535);
    CHECK_EQ(nx_roll_cell_p(&r, 2, 1000), 0);
    uint16_t p = nx_roll_cell_p(&r, 2, 1080);
    CHECK(p > 32768u && p < 65535u);                  /* ease-out: past half at half time */
    CHECK_EQ(nx_roll_cell_p(&r, 2, 1160), 65535);
    CHECK_EQ(nx_roll_cell_tint(&r, 2, 1000), 0);      /* starts with the first frame */
    CHECK_EQ(nx_roll_cell_tint(&r, 2, 1001), 32);
    CHECK_EQ(nx_roll_cell_tint(&r, 2, 1200), 16);
    CHECK_EQ(nx_roll_cell_tint(&r, 2, 1400), 0);
    CHECK(nx_roll_active(&r, 1399));
    CHECK(!nx_roll_active(&r, 1400));
    CHECK(strcmp(r.from, "728.000") == 0);            /* settled */

    /* right to left, 20 ms apart */
    nx_roll_init(&r, "0199");
    nx_roll_set(&r, "0200", 5000);
    CHECK(r.delay[3] == 0 && r.delay[2] == 1 && r.delay[1] == 2 && r.delay[0] == 0xFF);
    CHECK_EQ(nx_roll_cell_p(&r, 1, 5040), 0);
    CHECK(nx_roll_cell_p(&r, 1, 5041) > 0);

    /* live rule: more than 3 updates within a second swap instantly */
    nx_roll_init(&r, "1000");
    nx_roll_set(&r, "1001", 20000);
    nx_roll_set(&r, "1002", 20100);
    nx_roll_set(&r, "1003", 20200);
    CHECK(r.rolling);
    nx_roll_set(&r, "1004", 20300);
    CHECK(r.live);
    CHECK(!r.rolling);
    CHECK(strcmp(r.from, "1004") == 0);
    nx_roll_set(&r, "1005", 22000);                   /* a quiet second: rolling again */
    CHECK(!r.live);
    CHECK(r.rolling);

    /* reduced motion and a new length: instant */
    nx_anim_set_reduced(1);
    nx_roll_init(&r, "1");
    nx_roll_set(&r, "2", 100);
    CHECK(!r.rolling);
    nx_anim_set_reduced(0);
    nx_roll_set(&r, "10", 5000);
    CHECK(!r.rolling);
}

static void test_roll_draw(void)
{
    static uint16_t a[240 * 320], b[240 * 320];
    nx_gc ga = { a, { 0, 0, 320, 240 }, { 0, 0, 320, 240 }, nx_palettes[0], 0, 0, 0, 0 }, gb = ga;
    gb.px = b;
    nx_roll r;
    nx_roll_init(&r, "12.5");
    nx_roll_set(&r, "13.5", 100);
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    nx_roll_draw(&ga, &r, &nx_font_num48, 10, 20, 100, 0xFFFFu, 0x07FFu);   /* start: the old text */
    nx_text(&gb, &nx_font_num48, 10, 20, "12.5", 0xFFFFu);
    CHECK(memcmp(a, b, sizeof a) == 0);
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    nx_roll_draw(&ga, &r, &nx_font_num48, 10, 20, 600, 0xFFFFu, 0x07FFu);   /* settled, untinted */
    nx_text(&gb, &nx_font_num48, 10, 20, "13.5", 0xFFFFu);
    CHECK(memcmp(a, b, sizeof a) == 0);
    /* mid-roll: nothing leaves the cell */
    memset(a, 0, sizeof a);
    nx_roll_draw(&ga, &r, &nx_font_num48, 10, 20, 180, 0xFFFFu, 0x07FFu);
    int16_t cx[5];
    nx_text_cells(&nx_font_num48, "13.5", 10, cx, 4);
    int inside = 1;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            if (a[y * 320 + x] && (y < 20 || y >= 20 + nx_font_num48.height))
                inside = 0;
    CHECK(inside);
}

void test_nx_anim(void)
{
    test_ease();
    test_roll();
    test_roll_draw();
}
