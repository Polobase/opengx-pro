/* The settings record (src/nx/nx_settings.c) on the KV store. */
#include "harness.h"

#include "nx_config.h"
#include "nx_kv.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

void test_nx_settings(void)
{
    struct nx_settings s, t;

    /* nothing stored: the defaults */
    nxw_reset();
    g_ui_state = ST_HOME;
    nx_store_mount();
    CHECK_EQ(nx_settings_load(&s), NX_ENOTFOUND);
    CHECK_EQ(s.ver, NX_SETTINGS_VER);
    CHECK_EQ(s.resume_mode, NX_RESUME_ASK);
    CHECK_EQ(s.resume_countdown_s, 15);
    CHECK_EQ(s.sweep_tick_ms, 1000);
    CHECK_EQ(s.ui[0], 0);

    /* round trip; saving an unchanged record writes nothing */
    s.resume_mode = NX_RESUME_AUTO;
    s.session_limit_min[1] = 90;
    s.ui[0] = 200;
    s.ui[NX_SETTINGS_UI_BYTES - 1u] = 7;
    CHECK_EQ(nx_settings_save(&s), NX_OK);
    unsigned ops = flash_op_count;
    CHECK_EQ(nx_settings_save(&s), NX_OK);
    CHECK_EQ(flash_op_count, ops);
    nx_store_reset();
    nx_store_mount();
    CHECK_EQ(nx_settings_load(&t), NX_OK);
    CHECK(memcmp(&s, &t, sizeof s) == 0);

    /* an older, shorter record: its fields, the defaults for the rest */
    uint8_t old[8] = { 1, NX_RESUME_OFF, 1, 30, 5, 0, 6, 0 };
    CHECK_EQ(nx_kv_put(NX_KEY_SETTINGS, old, sizeof old), NX_OK);
    CHECK_EQ(nx_settings_load(&t), NX_OK);
    CHECK_EQ(t.resume_mode, NX_RESUME_OFF);
    CHECK_EQ(t.resume_countdown_s, 30);
    CHECK_EQ(t.session_limit_min[0], 5);
    CHECK_EQ(t.session_limit_min[1], 6);
    CHECK_EQ(t.sweep_tick_ms, 1000);
    /* a newer, longer record: the part this firmware knows */
    uint8_t newer[sizeof s + 20];
    memset(newer, 0x11, sizeof newer);
    newer[0] = 2;
    CHECK_EQ(nx_kv_put(NX_KEY_SETTINGS, newer, sizeof newer), NX_OK);
    CHECK_EQ(nx_settings_load(&t), NX_OK);
    CHECK_EQ(t.ver, NX_SETTINGS_VER);
    CHECK_EQ(t.resume_mode, 0x11);
    CHECK_EQ(t.ui[NX_SETTINGS_UI_BYTES - 1u], 0x11);

    /* in PC mode the save is refused (the record in RAM stays usable) */
    g_ui_state = ST_PC;
    s.ui[1] = 1;
    CHECK_EQ(nx_settings_save(&s), NX_REFUSED_PC);
}
