/* The UI's only door to the core (docs/nx/design/ui.md §6.2, §6.4): the screens never touch
 * g_ch[], the flags or the stores directly. The firmware implements these functions in
 * src/nx/nx_uicore.c on top of the real core; the gallery (APP=nxgallery, the host renderer)
 * implements them from fixtures in src/nx/nxg_core.c, so every screen renders without a core. */
#pragma once

#include <stdint.h>

#include "nx_bfb.h"
#include "nx_chain.h"
#include "nx_core.h"
#include "nx_hist.h"
#include "nx_keys.h"
#include "nx_manual.h"
#include "nx_terrain_data.h"
#include "nx_vm.h"

#define NX_UC_NAME_MAX 60u   /* NAME_LEN of state.h */
#define NX_UC_SLOTS 30u      /* PROGRAM_SLOTS of state.h: a VM slot above it is another source (nx_src.h) */
#define NX_UC_MANUAL 0x3Fu   /* NX_SRC_MANUAL of nx_src.h: the manual generator */
#define NX_UC_PRESET 0x30u   /* NX_SRC_PRESET of nx_src.h: built-in preset n is the source NX_UC_PRESET + n */

/* The snapshot of the core now (the UI adds its FINISHED latch and loop passes itself). */
void nx_uc_vm(struct nx_vm *vm);
/* A generator action (nx_core_act): NX_OK, NX_REFUSED_PC, NX_BUSY or NX_EINVAL. */
int nx_uc_act(const struct nx_action *a);
/* Name of slot 0..31, or of another source (nx_src.h), into out (NUL-terminated, at most
 * NX_UC_NAME_MAX bytes; control bytes and bytes the fonts do not have become '?'); returns the
 * length. */
unsigned nx_uc_name(unsigned slot, char *out);
/* Header of program slot 1..30 from the SPI flash (storage_slot_read); returns out->used. */
int nx_uc_slot_info(unsigned slot, struct nx_slot_info *out);
/* n frequencies of slot 1..30 from step index first (0-based); returns how many were read. */
unsigned nx_uc_slot_freqs(unsigned slot, unsigned first, uint64_t *out, unsigned n);
/* Calibration: a key of the calibration menu (1 = the grid needs a redraw), a value 0..28. */
int nx_uc_cal_key(const struct nx_kev *e);
int32_t nx_uc_cal_value(unsigned idx);
/* The UI's 32-byte blob in the settings record (nx_settings.h) and its save (NX_OK, or an error:
 * NX_REFUSED_PC in PC mode, the caller retries later). */
uint8_t *nx_uc_settings_blob(void);
int nx_uc_settings_save(void);
/* 1 when a blocking settings write (up to ~30 ms, the first one formats the store) cannot delay
 * a generator line: both queues idle and no running program reaches its next step within
 * need_ms. */
int nx_uc_quiet(uint32_t need_ms);
/* Resume after a power cut (nx_resume.h): the generators the boot offers to resume (bit ch; 0 = no
 * offer, also before the boot gate), the answer (1 = resume, 0 = discard), and the setting. */
unsigned nx_uc_resume_offer(void);
int nx_uc_resume_auto(void);   /* 1: the offer is the auto-start at power-on (plays from the start) */
void nx_uc_resume_answer(int resume);
int nx_uc_resume_on(void);
void nx_uc_resume_set(int on);
/* The panel's LCD init variant 'A' / 'B' (lcd_init at boot), '?' when unknown: About shows it. */
char nx_uc_panel(void);
/* Diagnostics (NX_FEAT_DIAG): the counters, sensors and SPI-flash id now. */
void nx_uc_diag(struct nx_diag_vm *d);
/* Core options the menu pages change (g_nx_settings; saved with the UI's settings). */
enum nx_opt {
    NX_OPT_STOP_G1,    /* NX_FEAT_TIMERS: G1 "stop after" minutes, 0 = off */
    NX_OPT_STOP_G2,    /* G2 */
    NX_OPT_OFF_DONE,   /* power off when all finished: 0 / 1 */
    NX_OPT_AUTOSTART,  /* start the last programs at power-on: 0 / 1 */
    NX_OPT_N
};
unsigned nx_uc_opt(unsigned id);
void nx_uc_opt_set(unsigned id, unsigned v);
/* History (NX_FEAT_HIST): records, i = 0 the newest (1 = valid), and the all-time totals. */
unsigned nx_uc_hist_count(void);
int nx_uc_hist_get(unsigned i, struct nx_hist_rec *r);
void nx_uc_hist_totals(struct nx_totals *t);
/* Slot copy / move / erase (NX_FEAT_SLOTS, nx_slots.h): the request's result, and whether a slot
 * holds a program. */
int nx_uc_slot_op(uint8_t op, unsigned from, unsigned to);
int nx_uc_slot_used(unsigned slot);
/* Chains (NX_FEAT_CHAINS, nx_chain.h): the NX_CHAINS chains (the editor changes them in place,
 * then asks for the save), and playing one on generator ch. */
struct nx_chain *nx_uc_chains(void);
void nx_uc_chains_save(void);
int nx_uc_chain_play(unsigned ch, unsigned id);
/* The built-in Terrain Protocol (NX_FEAT_TERRAIN, nx_terrain.h): protocols 1..count, their
 * position (or what a generator plays of them), play from the position, move it, the power. */
struct nx_tview {
    char name[20];      /* "Terrain", "Terrain-Mercury", ... */
    char set[40];       /* the set at the position */
    uint8_t kind;       /* NX_TP_REMOTE, NX_TP_CONTACT (nx_terrain_data.h) */
    uint8_t days;       /* of one round */
    uint8_t n, item;    /* items (Contact: sessions); item = n: complete */
    uint8_t day, part;  /* the item's day; Remote: 0 the whole day, 1/2 a half; Contact: session 1/2 */
    uint8_t hours;      /* Remote: the item's hours */
    uint8_t power;      /* Remote: 0..4 = 5, 2.5, 1, 0.5, 0.25 V */
    uint8_t playing;    /* bit ch: that generator plays it */
    uint32_t run_s;     /* Remote: running time in the item */
};
/* The manual generator (NX_FEAT_MANUAL, nx_manual.h): its values (changed in place, then
 * nx_uc_manual_changed), play on ch. */
struct nx_manual *nx_uc_manual(void);
void nx_uc_manual_changed(void);
int nx_uc_manual_play(unsigned ch);
/* The biofeedback scan (nx_bfb.h): its settings (changed in place), the start on
 * their generator (NX_OK or the refusal), the stop, and what it is doing. */
struct nx_bfb_cfg *nx_uc_bfb_cfg(void);
int nx_uc_bfb_start(void);
void nx_uc_bfb_stop(void);
void nx_uc_bfb_pause(void);
const struct nx_bfb_view *nx_uc_bfb(void);
unsigned nx_uc_terrain_count(void);
int nx_uc_terrain_view(unsigned id, struct nx_tview *v);
int nx_uc_terrain_play(unsigned ch, unsigned id);
void nx_uc_terrain_move(unsigned id, int delta);
void nx_uc_terrain_power(unsigned id);
/* Built-in presets with sweeps (NX_FEAT_PRESET, nx_preset.h): how many there are, and preset
 * n's steps (0: no such preset) with NX_UC_PRESET_LOOP set when it repeats until stopped. Its
 * name is nx_uc_name(NX_UC_PRESET + n); it is loaded and played as that source. */
#define NX_UC_PRESET_LOOP 0x100u   /* NX_PRESET_LOOP of nx_preset.h */
unsigned nx_uc_preset_count(void);
unsigned nx_uc_preset_info(unsigned n);
/* Multi-day plans (NX_FEAT_PLANS): the NX_PLANS plans (edited in place, saved with the chains),
 * and playing one's current day on generator ch. */
struct nx_plan *nx_uc_plans(void);
int nx_uc_plan_play(unsigned ch, unsigned id);
