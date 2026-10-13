/* nx settings record (nx_settings.h). */
#include "nx_settings.h"

#include <string.h>

#include "nx_config.h"
#include "nx_kv.h"

_Static_assert(sizeof(struct nx_settings) == 16u + NX_SETTINGS_UI_BYTES, "settings record layout");

struct nx_settings g_nx_settings;

void nx_settings_defaults(struct nx_settings *s)
{
    memset(s, 0, sizeof *s);
    s->ver = NX_SETTINGS_VER;
    s->resume_mode = NX_RESUME_ASK;
    s->resume_countdown_s = 15;
    s->sweep_tick_ms = 1000;
}

int nx_settings_load(struct nx_settings *s)
{
    uint8_t buf[sizeof *s + 64u];   /* room for a longer record from a newer firmware */
    nx_settings_defaults(s);
    int len = nx_kv_get(NX_KEY_SETTINGS, buf, sizeof buf);
    if (len < 1)
        return len < 0 ? len : NX_ENOTFOUND;
    unsigned n = (unsigned)len < sizeof buf ? (unsigned)len : sizeof buf;
    if (n > sizeof *s)
        n = sizeof *s;
    memcpy(s, buf, n);
    s->ver = NX_SETTINGS_VER;   /* in RAM it is this firmware's version */
    return NX_OK;
}

int nx_settings_save(const struct nx_settings *s)
{
    struct nx_settings stored;
    if (nx_kv_get(NX_KEY_SETTINGS, &stored, sizeof stored) == (int)sizeof stored
        && memcmp(&stored, s, sizeof stored) == 0)
        return NX_OK;
    return nx_kv_put(NX_KEY_SETTINGS, s, sizeof *s);
}
