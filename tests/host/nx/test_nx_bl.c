/* Backlight levels, fades and the w64 host override (src/nx/nx_bl.c; the TIM17/TIM3 drivers are
 * device code, their output is nx_hw_bl_set(), recorded by nx_world.c). */
#include "harness.h"

#include "nx_bl.h"
#include "nx_config.h"
#include "nx_world.h"
#include "test_nx.h"

void test_nx_bl(void)
{
    /* gamma 2 over one period; 0 and 255 are static levels; phases never shorter than 4 us */
    CHECK_EQ(nx_bl_on_us(0), 0);
    CHECK_EQ(nx_bl_on_us(255), NX_BL_PERIOD_US);
    CHECK_EQ(nx_bl_on_us(NX_BL_DEFAULT_LEVEL), 503);   /* 181^2 * 1000 / 65025: the original's 8/16 */
    CHECK_EQ(nx_bl_on_us(128), 251);
    CHECK_EQ(nx_bl_on_us(1), NX_BL_MIN_PHASE_US);
    CHECK_EQ(nx_bl_on_us(254), 992);
    for (unsigned l = 1; l < 255u; l++)
        CHECK(nx_bl_on_us((uint8_t)l) <= nx_bl_on_us((uint8_t)(l + 1u)));

    /* nx_bl_init leaves the output off; the user level is applied by the next tick */
    nxw_reset();
    CHECK_EQ(g_backlight_duty, NX_BL_W64_NONE);
    CHECK_EQ(g_pb7_duty, 1);
    nx_bl_set_user(NX_BL_DEFAULT_LEVEL);
    sim_advance_ms(1);
    CHECK_EQ(nx_bl_level(), NX_BL_DEFAULT_LEVEL);
    CHECK_EQ(nxw_bl_on_us, 503);
    CHECK_EQ(nxw_bl_period_us, NX_BL_PERIOD_US);

    /* a fade moves linearly and reaches the target exactly; the hardware only hears changes */
    nx_bl_fade_to(81, 100);
    sim_advance_ms(1);                        /* the tick picks the request up */
    sim_advance_ms(50);
    CHECK_EQ(nx_bl_level(), 130);             /* 181 - 100 * 51 / 100 */
    unsigned calls = nxw_bl_calls;
    sim_advance_ms(60);
    CHECK_EQ(nx_bl_level(), 81);
    CHECK(nxw_bl_calls > calls);
    calls = nxw_bl_calls;
    sim_advance_ms(20);
    CHECK_EQ(nxw_bl_calls, calls);
    nx_bl_fade_to(0, 0);
    sim_advance_ms(1);
    CHECK_EQ(nxw_bl_on_us, 0);

    /* w64 off (link.c writes g_backlight_duty = 0): off at once, fades cannot light it */
    nxw_reset();
    sim_advance_ms(1);
    g_backlight_duty = 0;
    sim_advance_ms(1);
    CHECK_EQ(nx_bl_host_off(), 1);
    CHECK_EQ(nx_bl_level(), 0);
    CHECK_EQ(nxw_bl_on_us, 0);
    nx_bl_fade_to(200, 10);
    sim_advance_ms(20);
    CHECK_EQ(nx_bl_level(), 0);
    CHECK_EQ(nxw_bl_on_us, 0);
    /* w64 on (8): back at the user's level */
    nx_bl_set_user(100);
    g_backlight_duty = 8;
    sim_advance_ms(2);
    CHECK_EQ(nx_bl_host_off(), 0);
    CHECK_EQ(nx_bl_level(), 100);
    CHECK_EQ(nxw_bl_on_us, nx_bl_on_us(100));
    CHECK_EQ(nx_bl_user(), 100);
    CHECK_EQ(g_backlight_duty, NX_BL_W64_NONE);   /* the tick took the value */

    /* every w64 on lights it, also after the UI dimmed it (the original's w64 on always did) */
    nx_bl_fade_to(0, 0);
    sim_advance_ms(1);
    CHECK_EQ(nx_bl_level(), 0);
    g_backlight_duty = 8;
    sim_advance_ms(1);
    CHECK_EQ(nx_bl_level(), 100);
    CHECK_EQ(nxw_bl_on_us, nx_bl_on_us(100));
}
