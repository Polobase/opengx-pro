/* nx panel driver (src/nx/nx_lcd.c) on the host ST7789 model (nx_panel.c): init sequence, fills,
 * bands, buffer ownership, the queue with asynchronous completion, fills longer than one DMA
 * transfer, the scroll mapping and the panel power states with their delays. */
#include "harness.h"

#include "nx_lcd.h"
#include "nx_panel.h"
#include "test_nx.h"

static void setup(void)
{
    nx_reset_world();
    nxp_reset();
    nx_lcd_init();
}

static int glass_rect_is(int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (nxp_at(x, y) != c)
                return 0;
    return 1;
}

static void test_init(void)
{
    setup();
    CHECK_EQ(nxp_madctl, NX_LCD_MADCTL);
    CHECK_EQ(nxp_nlog, 2);
    CHECK_EQ(nxp_log[0], NX_ST_VSCRDEF);
    CHECK_EQ(nxp_log[1], NX_ST_VSCSAD);
    CHECK(nxp_vscrdef[0] == 0 && nxp_vscrdef[1] == 320 && nxp_vscrdef[2] == 0);
    CHECK_EQ(nxp_vsp, 0);
    CHECK(nx_lcd_idle());
    CHECK_EQ(nx_lcd_room(), NX_LCD_QUEUE);
    CHECK_EQ(nx_lcd_power_state(), NX_LCD_ON);
    CHECK(nx_lcd_ready());
}

static void test_fill_and_band(void)
{
    setup();
    nx_rect r = { 10, 20, 30, 25 };
    CHECK(nx_lcd_fill(&r, 0xF800u));
    CHECK(glass_rect_is(10, 20, 30, 25, 0xF800u));
    CHECK_EQ(nxp_at(9, 20), 0);
    CHECK_EQ(nxp_at(30, 24), 0);
    CHECK_EQ(nxp_at(10, 25), 0);
    CHECK_EQ(nxp_pixels, 100);
    CHECK_EQ(g_nx_lcd_stats.fills, 1);
    /* window commands: CASET, RASET, RAMWR */
    CHECK(nxp_nlog == 5 && nxp_log[2] == NX_ST_CASET && nxp_log[3] == NX_ST_RASET && nxp_log[4] == NX_ST_RAMWR);

    uint16_t *b = nx_lcd_buf_get();
    CHECK(b != NULL);
    nx_rect br = { 100, 50, 140, 60 };
    for (int i = 0; i < 400; i++)
        b[i] = (uint16_t)(i * 7u + 1u);
    CHECK(nx_lcd_band(b, &br));
    int same = 1;
    for (int y = 50; y < 60; y++)
        for (int x = 100; x < 140; x++)
            if (nxp_at(x, y) != (uint16_t)(((y - 50) * 40 + (x - 100)) * 7 + 1))
                same = 0;
    CHECK(same);
    CHECK_EQ(g_nx_lcd_stats.bands, 1);
    CHECK_EQ(nxp_bad, 0);

    /* both buffers can be owned at once; a third request gets none */
    uint16_t *b1 = nx_lcd_buf_get(), *b2 = nx_lcd_buf_get();
    CHECK(b1 != NULL && b2 != NULL && b1 != b2);
    CHECK(nx_lcd_buf_get() == NULL);
    nx_rect big = { 0, 0, 320, 9 };                    /* 2880 > NX_BAND_PX */
    CHECK_EQ(nx_lcd_band(b1, &big), 0);
    nx_rect empty = { 5, 5, 5, 9 };
    CHECK_EQ(nx_lcd_band(b1, &empty), 0);
    nx_lcd_buf_put(b1);
    nx_lcd_buf_put(b2);
    CHECK(nx_lcd_buf_get() != NULL);
}

static void test_queue_async(void)
{
    setup();
    nxp_async = 1;
    static const uint16_t colors[5] = { 0x1111u, 0x2222u, 0x3333u, 0x4444u, 0x5555u };
    for (unsigned i = 0; i < 4u; i++) {
        nx_rect r = { (int16_t)(i * 10u), 0, (int16_t)(i * 10u + 20u), 10 };   /* overlapping */
        CHECK(nx_lcd_fill(&r, colors[i]));
    }
    CHECK_EQ(nx_lcd_room(), 0);
    nx_rect r5 = { 0, 0, 1, 1 };
    CHECK_EQ(nx_lcd_fill(&r5, colors[4]), 0);          /* queue full */
    CHECK(!nx_lcd_idle());
    unsigned n = 0;
    while (nxp_complete())
        n++;
    CHECK_EQ(n, 4);
    CHECK(nx_lcd_idle());
    CHECK_EQ(nxp_bad, 0);                              /* no command while a transfer ran */
    CHECK_EQ(nxp_at(5, 5), 0x1111u);
    CHECK_EQ(nxp_at(15, 5), 0x2222u);                  /* later fills win */
    CHECK_EQ(nxp_at(35, 5), 0x4444u);
    CHECK_EQ(nxp_at(45, 5), 0x4444u);

    /* a full-screen fill is two DMA transfers (65535 + 11265 pixels) */
    nx_rect full = { 0, 0, 320, 240 };
    CHECK(nx_lcd_fill(&full, 0x07E0u));
    n = 0;
    while (nxp_complete())
        n++;
    CHECK_EQ(n, 2);
    CHECK(glass_rect_is(0, 0, 320, 240, 0x07E0u));

    /* a band's buffer comes back only when its transfer has finished */
    uint16_t *b = nx_lcd_buf_get(), *b2 = nx_lcd_buf_get();
    nx_rect br = { 0, 0, 320, 8 };
    for (int i = 0; i < 2560; i++)
        b[i] = 0xFFFFu;
    CHECK(nx_lcd_band(b, &br));
    CHECK(nx_lcd_buf_get() == NULL);
    nxp_complete();
    CHECK(nx_lcd_buf_get() == b);
    nx_lcd_buf_put(b);
    nx_lcd_buf_put(b2);
    nxp_async = 0;
}

static void test_scroll(void)
{
    setup();
    nx_rect col8 = { 8, 0, 9, 240 };
    CHECK(nx_lcd_fill(&col8, 0xF800u));
    CHECK(nx_lcd_scroll(8));
    CHECK_EQ(nx_lcd_scroll_pos(), 8);
    CHECK_EQ(nxp_vsp, NX_LCD_SCROLL_SIGN > 0 ? 312 : 8);
    CHECK_EQ(nxp_at(0, 100), 0xF800u);                 /* the glass shows GRAM column X + 8 at X */
    CHECK_EQ(nxp_at(8, 100), 0);
    CHECK(nx_lcd_scroll(320));
    CHECK_EQ(nx_lcd_scroll_pos(), 0);
    CHECK_EQ(nxp_at(8, 100), 0xF800u);
}

static void test_power(void)
{
    setup();                                           /* at 0 ms: SLPOUT counts from here */
    nx_lcd_power(NX_LCD_SLEEP);
    nx_lcd_power_step(10);
    CHECK_EQ(nxp_disp_on, 0);                          /* DISPOFF first */
    CHECK_EQ(nxp_sleep, 0);
    nx_lcd_power_step(100);
    CHECK_EQ(nxp_sleep, 0);                            /* SLPIN not within 120 ms of SLPOUT */
    nx_lcd_power_step(120);
    CHECK_EQ(nxp_sleep, 1);
    CHECK_EQ(nx_lcd_power_state(), NX_LCD_SLEEP);
    CHECK(!nx_lcd_ready());
    CHECK_EQ(nxp_at(0, 0), 0);

    nx_lcd_power(NX_LCD_BLANK);                        /* wake into the dark */
    nx_lcd_power_step(200);
    CHECK_EQ(nxp_sleep, 1);                            /* 120 ms after SLPIN at the earliest */
    nx_lcd_power_step(240);
    CHECK_EQ(nxp_sleep, 0);
    CHECK(!nx_lcd_ready());                            /* 5 ms after SLPOUT */
    nx_lcd_power_step(244);
    CHECK(!nx_lcd_ready());
    nx_lcd_power_step(245);
    CHECK(nx_lcd_ready());
    CHECK_EQ(nxp_disp_on, 0);
    nx_lcd_power(NX_LCD_ON);
    nx_lcd_power_step(246);
    CHECK_EQ(nxp_disp_on, 1);
    CHECK_EQ(nx_lcd_power_state(), NX_LCD_ON);
    CHECK_EQ(nxp_bad, 0);
}

void test_nx_lcd(void)
{
    test_init();
    test_fill_and_band();
    test_queue_async();
    test_scroll();
    test_power();
}
