/* Settings through the keys on the host board model: DOWN on the dashboard opens them, a changed
 * theme and brightness apply at once and are saved once the pages are closed, every page opens
 * and STOP walks back to the dashboard; About opens the support page on the last stack level. */
#include "harness.h"

#include "nx_anim.h"
#include "nx_config.h"
#include "nx_keys.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

static void press(uint8_t keys, uint32_t hold_ms)
{
    nxw_press(keys, hold_ms);
    nxw_run_ms(250);   /* a transition (push) is over */
}

static void boot_quiet(void)
{
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.splash = NX_SPLASH_OFF;
    u.guide = 1;   /* not a first boot: no key guide */
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    nxw_reset();
    nx_store_mount();
    nxw_run_until(4600);
}

static void test_display(void)
{
    boot_quiet();
    press(NX_KEY_DOWN, 100);    /* the menu, on Settings */
    CHECK(nx_ui_top() == &nx_scr_menu);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_set);
    CHECK_EQ(nx_ui_depth(), 2);
    press(NX_KEY_START, 100);   /* Display */
    CHECK_EQ(nx_ui_depth(), 3);
    press(NX_KEY_G2, 100);      /* Dark -> Light */
    CHECK_EQ(g_uiset.theme, NX_THEME_LIGHT);
    CHECK_EQ(nx_render_theme_get(), NX_THEME_LIGHT);
    press(NX_KEY_DOWN, 100);
    press(NX_KEY_G1, 100);      /* brightness 70 -> 60 % */
    CHECK_EQ(g_uiset.bright, 6);
    struct nx_settings s;
    CHECK(nx_settings_load(&s) != NX_OK || s.ui[1] != NX_THEME_LIGHT);   /* not while the pages are open */
    press(NX_KEY_STOP, 100);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_dash);
    nxw_run_ms(3500);
    CHECK_EQ(nx_settings_load(&s), NX_OK);
    CHECK_EQ(s.ui[0], NX_UISET_VER);
    CHECK_EQ(s.ui[1], NX_THEME_LIGHT);
    CHECK_EQ(s.ui[2], 6);
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}

static void test_pages(void)
{
    boot_quiet();
    /* the root's four links (Display, Keys & lock, Motion, About) each open a page on top */
    press(NX_KEY_DOWN, 100);    /* the menu, on Settings */
    press(NX_KEY_START, 100);
    for (int i = 0; i < 4; i++) {
        press(NX_KEY_START, 100);
        CHECK(nx_ui_top() == &nx_scr_set);
        CHECK_EQ(nx_ui_depth(), 3);
        press(NX_KEY_STOP, 100);
        CHECK_EQ(nx_ui_depth(), 2);
        press(NX_KEY_DOWN, 100);
    }
    /* the root ends at "Reset all settings": START asks for a hold-START confirmation, STOP
     * cancels it and nothing changes */
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_confirm);
    CHECK_EQ(nx_ui_depth(), 3);
    press(NX_KEY_STOP, 100);
    CHECK_EQ(nx_ui_depth(), 2);
    /* Motion: START on "Reduce motion" flips it; the engine follows */
    press(NX_KEY_UP, 100);
    press(NX_KEY_UP, 100);
    press(NX_KEY_START, 100);
    press(NX_KEY_START, 100);
    CHECK_EQ(g_uiset.motion, 1);
    CHECK(nx_anim_reduced());
    press(NX_KEY_STOP, 100);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}

#if NX_FEAT_SUPPORT
/* Settings > About > Support the project: the fifth row opens the QR page on the stack's last
 * level; no key there opens anything or acts on the generators, STOP and START go back. */
static void test_support(void)
{
    boot_quiet();
    press(NX_KEY_DOWN, 100);    /* the menu, on Settings */
    press(NX_KEY_START, 100);
    for (int i = 0; i < 3; i++)
        press(NX_KEY_DOWN, 100);
    press(NX_KEY_START, 100);   /* About */
    CHECK_EQ(nx_ui_depth(), 3);
    for (int i = 0; i < 4; i++)
        press(NX_KEY_DOWN, 100);
    press(NX_KEY_START, 100);   /* Support the project */
    CHECK(nx_ui_top() == &nx_scr_support);
    CHECK_EQ(nx_ui_depth(), NX_UI_DEPTH);
    static const uint8_t k_keys[] = { NX_KEY_UP, NX_KEY_DOWN, NX_KEY_G1, NX_KEY_G2, NX_KEY_LOOP };
    for (unsigned i = 0; i < sizeof k_keys; i++) {
        press(k_keys[i], 100);
        CHECK(nx_ui_top() == &nx_scr_support);
        CHECK_EQ(nx_ui_depth(), NX_UI_DEPTH);
    }
    CHECK(!(g_ui_flags & (UIF_RUN_G1 | UIF_RUN_G2)));
    press(NX_KEY_START, 100);   /* back to About, on the same row */
    CHECK(nx_ui_top() == &nx_scr_set);
    CHECK_EQ(nx_ui_depth(), 3);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_support);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_set);
    press(NX_KEY_STOP, 100);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}
#endif

#if NX_FEAT_DIAG
/* menu > Diagnostics: values once a second, LOOP opens the key test, whose keys act on nothing
 * (G1+G2 held does not lock), STOP held leaves it, STOP closes Diagnostics */
static void test_diag(void)
{
    boot_quiet();
    nx_scr_menu_preselect(&nx_scr_diag, 0);
    press(NX_KEY_DOWN, 100);    /* the menu, on Diagnostics */
    CHECK(nx_ui_top() == &nx_scr_menu);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_diag);
    press(NX_KEY_DOWN, 100);    /* scrolls */
    CHECK(nx_ui_top() == &nx_scr_diag);
    press(NX_KEY_LOOP, 100);
    CHECK(nx_ui_top() == &nx_scr_keytest);
    press(NX_KEY_G1 | NX_KEY_G2, 2000);
    CHECK(nx_ui_top() == &nx_scr_keytest);
    CHECK(!nx_ui_locked());
    press(NX_KEY_STOP, 100);    /* a short STOP is only shown */
    CHECK(nx_ui_top() == &nx_scr_keytest);
    press(NX_KEY_STOP, 1700);
    CHECK(nx_ui_top() == &nx_scr_diag);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(nx_ui_depth(), 1);
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}
#endif

void test_nx_ui_set(void)
{
    test_display();
    test_pages();
#if NX_FEAT_SUPPORT
    test_support();
#endif
#if NX_FEAT_DIAG
    test_diag();
#endif
}
