/* nx settings record (docs/nx/design/storage-runner-features.md §1.5 key 0x0001 SETTINGS, §1.10):
 * core settings plus an opaque blob that belongs to the UI (docs/nx/design/ui.md §6.4 item 8).
 * Version 1. Loading starts from the defaults and copies what the stored record has, so a shorter
 * (older) record keeps the defaults of the new fields and a longer (newer) one loses only what
 * this firmware does not know. Saving writes only when something changed. */
#pragma once

#include <stdint.h>

#define NX_KEY_SETTINGS 0x0001u
#define NX_SETTINGS_VER 1u
#define NX_SETTINGS_UI_BYTES 32u

enum nx_resume_mode { NX_RESUME_OFF = 0, NX_RESUME_ASK, NX_RESUME_AUTO_COUNTDOWN, NX_RESUME_AUTO };

struct nx_settings {
    uint8_t ver;                   /* NX_SETTINGS_VER */
    uint8_t resume_mode;           /* enum nx_resume_mode */
    uint8_t resume_after_clean_off;
    uint8_t resume_countdown_s;
    uint16_t session_limit_min[2]; /* per generator, 0 = none */
    uint16_t sweep_tick_ms;
    uint8_t live_apply;            /* 0 = safe pause/apply/resume for amplitude/waveform changes */
    uint8_t ampl_cap_v;            /* 0 = no amplitude cap */
    uint8_t off_when_done;         /* 1: power off once every program has finished (nx_timer.h) */
    uint8_t autostart;             /* 1: offer the last programs at power-on (nx_resume.h) */
    uint8_t rsv[2];
    uint8_t ui[NX_SETTINGS_UI_BYTES];   /* owned by the UI (nx_uiset.h): theme, brightness, ... */
};

extern struct nx_settings g_nx_settings;   /* loaded at boot (main_nx.c) */

void nx_settings_defaults(struct nx_settings *s);
/* Defaults, then the stored record (if any). Returns NX_OK, or NX_ENOTFOUND (defaults kept). */
int nx_settings_load(struct nx_settings *s);
/* Stores s unless the stored record is identical. NX_OK or an nx_kv_put() error. */
int nx_settings_save(const struct nx_settings *s);
