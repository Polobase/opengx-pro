/* The UI's settings: the 32-byte blob the settings record keeps for the UI (nx_settings.h
 * `ui[]`, NX_SETTINGS_UI_BYTES). Layout version 2, byte for byte:
 *
 *   0  ver        NX_UISET_VER (2) or a later layout; lower = UI defaults (a fresh record is 0)
 *   1  theme      NX_THEME_DARK / NX_THEME_LIGHT / NX_THEME_HC
 *   2  bright     brightness in 10 % steps, 1..10
 *   3  dim        dim after: index into k_nx_dim_s (never, 30 s, 1, 2, 5, 10 min)
 *   4  off        screen off after dimming: index into k_nx_off_s (never, 1, 5, 15, 30 min)
 *   5  night      night palette on (quick settings)
 *   6  motion     0 = full, 1 = reduced
 *   7  marquee    1 = long names scroll
 *   8  repeat     key repeat: 0 slow, 1 normal, 2 fast
 *   9  autolock   1 = lock the keys 10 s after the last key while a generator runs
 *  10  splash     0 off, 1 short, 2 full
 *  11  idle       after the "off" time: 0 = screen off, 1 = night view (NX_IDLE_*)
 *  12  guide      1 = the first-boot key guide was shown
 *  13  origkeys   1 = the original's keys: POWER short stops both, START / LOOP as the original,
 *                 the original's key repeat
 *  14..31         0 (reserved: a later layout appends its fields here and keeps bytes 1..13)
 *
 * Bytes 11 to 13 came later within version 2: they were reserved (0) before, and 0 is their
 * default, so an older blob loads unchanged (the key guide then shows once).
 *
 * A blob of a later layout loads its bytes 1..13 as these fields; its version and the bytes
 * after them stay in struct nx_uiset and go back into the blob unchanged, so a firmware of that
 * layout still finds its own fields after this one saved a setting. A lower version is the
 * development layout 1, which no flashed image wrote (its fields sit at other offsets): it
 * loads as the defaults. */
#pragma once

#include <stdint.h>

#define NX_UISET_VER 2u
#define NX_UISET_BYTES 32u

enum { NX_SPLASH_OFF = 0, NX_SPLASH_SHORT, NX_SPLASH_FULL };
enum { NX_IDLE_OFF = 0, NX_IDLE_AMBIENT };

struct nx_uiset {
    uint8_t ver, theme, bright, dim, off, night, motion, marquee, repeat, autolock, splash, idle, guide, origkeys;
    uint8_t rsv[18];
};

_Static_assert(sizeof(struct nx_uiset) == NX_UISET_BYTES, "UI settings blob layout v2");

#define NX_DIM_N 6u
#define NX_OFF_N 5u
extern const uint16_t k_nx_dim_s[NX_DIM_N];   /* 0 = never */
extern const uint16_t k_nx_off_s[NX_OFF_N];   /* 0 = never */

extern struct nx_uiset g_uiset;   /* the settings in use */

void nx_uiset_defaults(struct nx_uiset *u);
/* Blob -> settings: defaults below NX_UISET_VER; else the fields of this layout, each clamped to
 * its range, with the version and the bytes after them as they were. */
void nx_uiset_from_blob(struct nx_uiset *u, const uint8_t blob[NX_UISET_BYTES]);
void nx_uiset_to_blob(const struct nx_uiset *u, uint8_t blob[NX_UISET_BYTES]);
uint8_t nx_uiset_level(const struct nx_uiset *u);   /* backlight level 0..255 of the brightness */
