/* The nx UI framework's interface for its components (nx_comp.c) and screens (nx_scr_*.c)
 * (docs/nx/design/ui.md §5, §6). nx_ui.c owns the screen stack, the layers, the overlays
 * (toasts, key-lock card, hold rings), the power states (dim, off, night view), the key lock and
 * the action outbox; a screen is a table of callbacks working on a state block the framework
 * keeps per stack level.
 *
 * Frame consistency: key events, core events and the VM are applied in nx_ui_tick; at every
 * frame start the framework copies g_vm to g_vm_draw and each level's live state to its draw
 * copy, and draw() callbacks read only those copies (and nx_ui_now(), the frame's time). A
 * screen's tick() compares g_vm with g_vm_seen (the VM of the last invalidation) and its own
 * state with what it last drew, and invalidates the rectangles that changed. */
#pragma once

#include <stdint.h>

#include "nx_assets.h"
#include "nx_font.h"
#include "nx_gfx.h"
#include "nx_keys.h"
#include "nx_render.h"
#include "nx_time.h"
#include "nx_uicore.h"
#include "nx_uiset.h"
#include "nx_vm.h"

#define NX_UI_DEPTH 4u     /* screen stack (ui.md §5.7) */
#define NX_UI_ST_MAX 64u   /* bytes of state per stack level */
#define NX_NONE 0xFFFFu    /* no string: no key-hint bar */

/* Zones (ui.md §4.3) */
#define NX_BAR_H 22        /* status / title bar 0..21 */
#define NX_HINT_Y 216      /* key-hint bar 216..239 */
#define NX_M 12            /* screen margin */

/* Transitions of nx_ui_push / nx_ui_pop (ui.md §2.11) */
enum nx_tr {
    NX_TR_NONE = 0,   /* instant */
    NX_TR_PUSH,       /* hardware-scroll push: the new page enters from the right */
    NX_TR_BACK,       /* push the other way: back to the page before */
    NX_TR_DIP,        /* backlight dip: hierarchy changes (PC mode in and out) */
    NX_TR_SHEET,      /* bottom sheet revealed over the scrimmed screen */
};

/* Screen flags */
#define NX_SF_SHEET 0x01u   /* a bottom sheet: drawn in the sheet layer over the level below */
#define NX_SF_RAW   0x02u   /* every key event goes to the screen: no lock, no global keys */
#define NX_SF_AWAKE 0x04u   /* no auto dim / off while shown */
#define NX_SF_PASS  0x08u   /* a key press closes the screen and then acts on the one below */

struct nx_scr {
    void (*enter)(void *st, uint32_t arg);             /* state block zeroed before */
    int (*key)(void *st, const struct nx_kev *e);      /* 1 = handled */
    void (*tick)(void *st, uint32_t now);              /* invalidate what changed; may be NULL */
    void (*draw)(nx_gc *gc, const void *st);           /* the frame copy of the state */
    uint16_t hint;       /* string of the key-hint bar (the framework draws it), NX_NONE: none */
    uint8_t flags;       /* NX_SF_* */
    uint8_t repeat;      /* keys that auto-repeat on this screen (NX_KEY_*) */
    uint8_t sheet_h;     /* NX_SF_SHEET: height of the sheet at the bottom of the screen */
    uint8_t st_size;     /* <= NX_UI_ST_MAX */
};

/* The screens (nx_scr_*.c) */
extern const struct nx_scr nx_scr_dash, nx_scr_menu, nx_scr_quick, nx_scr_lib, nx_scr_detail, nx_scr_set, nx_scr_pc, nx_scr_cal,
    nx_scr_splash, nx_scr_guide, nx_scr_guide_boot, nx_scr_bye, nx_scr_confirm;
extern const struct nx_scr nx_scr_diag, nx_scr_keytest;   /* NX_FEAT_DIAG */
extern const struct nx_scr nx_scr_hist;                    /* NX_FEAT_HIST */
extern const struct nx_scr nx_scr_slotact;                 /* NX_FEAT_SLOTS */
extern const struct nx_scr nx_scr_chains, nx_scr_chain_edit;   /* NX_FEAT_CHAINS */
extern const struct nx_scr nx_scr_plan_edit;                   /* NX_FEAT_PLANS */
extern const struct nx_scr nx_scr_terrain;                     /* NX_FEAT_TERRAIN: argument id | generator << 8 */
extern const struct nx_scr nx_scr_manual;                      /* NX_FEAT_MANUAL: argument 0x100 | generator, or 0: the focus */
extern const struct nx_scr nx_scr_support;                     /* NX_FEAT_SUPPORT */
void nx_scr_terrain_started(unsigned ch, unsigned id);         /* the dashboard of ch and the toast */
#define NX_SET_TIMERS 5u   /* nx_scr_set's argument for the Timers page (menu, NX_FEAT_TIMERS) */

/* Power-on value of what the library remembers between openings (its selection); nx_ui_init
 * calls it. */
void nx_scr_lib_boot(void);
void nx_scr_menu_boot(void);   /* the menu's last item: Settings */
#ifdef HOST_TEST
void nx_scr_menu_preselect(const struct nx_scr *scr, uint8_t arg);   /* the menu opens on this item */
#endif
/* The factory port set calibration value idx while the calibration screen (state st) shows. */
void nx_scr_cal_factory(void *st, unsigned idx);
/* The night view (nx_scr_sys.c): drawn instead of the screen stack when idle, night palette, one
 * update per second. */
void nx_ambient_draw(nx_gc *gc);

extern struct nx_vm g_vm;        /* the core now (plus the UI's latches) */
extern struct nx_vm g_vm_seen;   /* the VM of the last invalidation */
extern struct nx_vm g_vm_draw;   /* the VM of the frame being drawn */

/* ---- stack ---- */
void nx_ui_push(const struct nx_scr *s, uint32_t arg, uint8_t tr);
void nx_ui_pop(uint8_t tr);
void nx_ui_reset(const struct nx_scr *s, uint32_t arg, uint8_t tr);   /* the stack is s alone */
unsigned nx_ui_depth(void);
const struct nx_scr *nx_ui_top(void);

/* ---- time, state ---- */
uint32_t nx_ui_now(void);        /* drawing: the time the frame started */
uint8_t nx_ui_focus(void);       /* the generator the dashboard shows (0 = G1) */
void nx_ui_set_focus(uint8_t ch);
int nx_ui_motion(void);          /* marquees and the scope move: not reduced, not dimmed */
int nx_ui_locked(void);

/* ---- actions (outbox to the core; the core refuses generator actions in PC mode) ---- */
int nx_ui_act(uint8_t type, uint8_t ch, uint16_t id, uint32_t v);

/* ---- overlays ---- */
/* A toast (ui.md W21): string sid with up to two arguments (at most 43 bytes are kept), icon
 * NX_I20_* (0xFF none) in palette token tok. */
void nx_ui_toast_s(uint8_t icon, uint8_t tok, uint16_t sid, const char *a0, const char *a1);
/* Hold ring overlay (POWER, STOP-hold): label sid, a 32 px icon (NX_I32_*, or NX_UI_ICON20 |
 * NX_I20_* for a 20 px one) in the ring, filling from from_ms to to_ms after t0. */
#define NX_UI_ICON20 0x80u
void nx_ui_hold_show(uint16_t sid, uint8_t icon, uint32_t t0, uint16_t from_ms, uint16_t to_ms);
void nx_ui_hold_hide(void);
/* Hold-START confirmation for destructive actions (ui.md §5.5): title and body strings; yes()
 * runs after START was held NX_CONFIRM_MS; STOP cancels. */
#define NX_CONFIRM_MS 1000u
struct nx_confirm {
    uint16_t title, body;
    void (*yes)(void);
};
void nx_ui_confirm(uint16_t title, uint16_t body, void (*yes)(void));

/* ---- settings ---- */
void nx_ui_apply(void);          /* g_uiset changed: theme, brightness, motion, repeat */
void nx_ui_settings_dirty(void); /* saved when nx_ui_settings_flush runs */
void nx_ui_settings_flush(void); /* save now if dirty (not in PC mode: then after PC mode) */
void nx_ui_lock(int on);         /* quick settings: lock the keys */
void nx_ui_screen_off(void);     /* quick settings / POWER: screen off */
