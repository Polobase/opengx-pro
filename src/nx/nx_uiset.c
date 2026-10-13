/* UI settings blob v2 (nx_uiset.h). */
#include "nx_uiset.h"

#include <string.h>

#include "nx_assets.h"

const uint16_t k_nx_dim_s[NX_DIM_N] = { 0, 30, 60, 120, 300, 600 };
const uint16_t k_nx_off_s[NX_OFF_N] = { 0, 60, 300, 900, 1800 };

struct nx_uiset g_uiset;

void nx_uiset_defaults(struct nx_uiset *u)
{
    memset(u, 0, sizeof *u);
    u->ver = NX_UISET_VER;
    u->theme = NX_THEME_DARK;
    u->bright = 7;          /* 70 %: level 179, about the classic 50 % duty */
    u->dim = 2;             /* 1 min */
    u->off = 2;             /* then 5 min */
    u->marquee = 1;
    u->repeat = 1;
    u->splash = NX_SPLASH_FULL;
}

static uint8_t clamp(uint8_t v, uint8_t lo, uint8_t hi, uint8_t def)
{
    return v < lo || v > hi ? def : v;
}

void nx_uiset_from_blob(struct nx_uiset *u, const uint8_t blob[NX_UISET_BYTES])
{
    struct nx_uiset d;
    nx_uiset_defaults(&d);
    if (blob[0] < NX_UISET_VER) {   /* a fresh record (0) or the development layout 1 */
        *u = d;
        return;
    }
    memcpy(u, blob, sizeof *u);   /* the version and rsv[] too: a later layout's fields */
    u->theme = clamp(u->theme, NX_THEME_DARK, NX_THEME_HC, d.theme);
    u->bright = clamp(u->bright, 1, 10, d.bright);
    u->dim = clamp(u->dim, 0, NX_DIM_N - 1u, d.dim);
    u->off = clamp(u->off, 0, NX_OFF_N - 1u, d.off);
    u->night = clamp(u->night, 0, 1, 0);
    u->motion = clamp(u->motion, 0, 1, 0);
    u->marquee = clamp(u->marquee, 0, 1, d.marquee);
    u->repeat = clamp(u->repeat, 0, 2, d.repeat);
    u->autolock = clamp(u->autolock, 0, 1, 0);
    u->splash = clamp(u->splash, 0, 2, d.splash);
    u->idle = clamp(u->idle, 0, 1, d.idle);
    u->guide = clamp(u->guide, 0, 1, 0);
    u->origkeys = clamp(u->origkeys, 0, 1, 0);
}

void nx_uiset_to_blob(const struct nx_uiset *u, uint8_t blob[NX_UISET_BYTES])
{
    memcpy(blob, u, NX_UISET_BYTES);
}

uint8_t nx_uiset_level(const struct nx_uiset *u)
{
    return (uint8_t)(u->bright * 255u / 10u);   /* perceptual levels: nx_bl applies the gamma */
}
