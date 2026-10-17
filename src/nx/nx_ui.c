/* The nx UI framework (nx_ui.h for the core, nx_ui_int.h for the screens): event intake, the
 * VM snapshot and its frame copies, the screen stack, layers and transitions, overlays, the
 * power states, the key lock, the action outbox and the settings.
 *
 * Power states (ui.md §2.12, §5.1): ACTIVE -> DIM after the "dim after" time without a key
 * (backlight to a third), -> OFF or the night view (Settings > Display > When idle) after the
 * "off after" time more. POWER short switches the screen off and on. With the screen off or in
 * the night view the first key only wakes; dimmed, the key acts and the brightness comes back. Attention (a program finished, a generator stopped answering, PC mode
 * changed) wakes the screen.
 * Key lock (ui.md §5.5): G1+G2 held 1.5 s locks, START+STOP held 1.5 s unlocks; while locked,
 * a key shows the lock card for 2 s; POWER still switches the screen and powers off. */
#include "nx_ui.h"

#include <string.h>

#include "nx_anim.h"
#include "nx_bl.h"
#include "nx_comp.h"
#include "nx_config.h"
#include "nx_lcd.h"
#include "nx_time.h"
#include "nx_trans.h"
#include "nx_uifmt.h"

#define EVQ 8u
#define TOASTQ 4u
#define TOAST_LEN 44u
#define TOAST_MS 4000u
#define TOAST_IN_MS 200u
#define TOAST_OUT_MS 160u
#define TOAST_AREA ((nx_rect){ 12, 4, 308, 40 })
#define LOCK_CARD_MS 2000u
#define ALERT_MS 60000u        /* the set-end card closes itself */
#define ALERT_PULSE_MS 500u    /* its backlight pulses */
#define LOCK_HOLD_MS 1500u        /* NX_KEY_LONG_MS: lock and unlock chords */
#define AUTOLOCK_MS 10000u
#define POWER_RING_MS 200u        /* the ring shows from 200 ms ... */
#define OFF_FADE_MS 600u
#define WAKE_FADE_MS 150u
#define DIM_FADE_MS 1200u
#define AMBIENT_LEVEL 44u         /* the night view's backlight: 44^2 / 255^2 = 3 % */
#define VM_MS 10u                 /* the VM is rebuilt at most every 10 ms */
#define BYE_FADE_MS 300u          /* the goodbye's last 300 ms: backlight out, then the panel sleeps */
#define RESUME_MS 15000u          /* the resume offer resumes by itself after this */
#define NO_SID 0xFFFFu
#define BYE_SLEEP_MS 40u
#define SAVE_IDLE_MS 3000u        /* settings are written 3 s after the last key ... */
#define SAVE_QUIET_MS 1000u       /* ... when no generator line is due within 1 s */

enum { PW_ACTIVE, PW_DIM, PW_FADING, PW_OFF, PW_WAKING, PW_AMBIENT };

struct level {
    const struct nx_scr *scr;
    uint8_t live[NX_UI_ST_MAX];
    uint8_t draw[NX_UI_ST_MAX];
};

struct toast {
    uint8_t icon, tok;
    char text[TOAST_LEN];
};

struct nx_vm g_vm, g_vm_seen, g_vm_draw;

static struct level s_lvl[NX_UI_DEPTH];
static uint8_t s_depth;
static uint32_t s_frame_ms;
static uint8_t s_focus;

static struct nx_event s_evq[EVQ];
static uint8_t s_ev_head, s_ev_tail;
static uint32_t s_vm_t;
static uint8_t s_finished[2];    /* FINISHED latch per generator */
static uint16_t s_passes[2];

static uint8_t s_pw, s_night_on;   /* s_night_on: the night view's palette */
static uint32_t s_pw_t, s_input_t;
static uint8_t s_ignore;         /* keys whose gesture belongs to the screen they left */
static uint8_t s_blocking;       /* a push, dip or reveal runs: keys wait */
static uint8_t s_scrim;
static uint8_t s_locked;
static uint32_t s_lock_card_t;   /* lock card shown since (0 = hidden) */
static uint32_t s_unlock_t0;     /* START+STOP chord down since */
static uint32_t s_power_t0;      /* POWER down since (0 = up) */
/* The offer cards over the screen: resume after a power cut, power off when all finished, a
 * Terrain Contact session's end. */
enum { CARD_NONE, CARD_RESUME, CARD_DONE, CARD_AUTO, CARD_ALERT };
static uint8_t s_card;           /* CARD_*: the card shown */
static uint8_t s_done_due;       /* NXE_ALL_DONE arrived: the power-off card is due */
static uint8_t s_alert_due;      /* NXE_ALERT arrived: the set-end card is due */
static uint8_t s_pulse;          /* the set-end card's backlight pulses so far */
static uint32_t s_card_t0;       /* shown since */
static void card_answer(int yes);
static uint8_t s_save_due;
static uint8_t s_bye, s_bye_dark, s_bye_faded, s_bye_slept;
static uint32_t s_bye_t;         /* the goodbye's last step (ms) */

static struct toast s_toastq[TOASTQ], s_toast, s_toast_draw;
static uint8_t s_toastn;
static uint8_t s_toast_phase;    /* 0 none, 1 in, 2 shown, 3 out */
static uint32_t s_toast_t;

static struct {
    uint16_t sid, sid2;   /* the card's line, and a caption line under it (NO_SID: none) */
    uint8_t icon, on;
    uint32_t t0;
    uint16_t from, to;
} s_hold, s_hold_draw;
static uint8_t s_lock_draw, s_pw_draw;
static uint32_t s_unlock_draw;

#ifdef HOST_TEST
struct nx_action g_ui_actlog[16];   /* the host tests read what the UI asked the core for */
unsigned g_ui_actn;
#endif

/* ---- small helpers ---- */

static struct level *top_level(void)
{
    return s_depth ? &s_lvl[s_depth - 1u] : 0;
}

static struct level *base_level(void)
{
    if (!s_depth)
        return 0;
    struct level *t = &s_lvl[s_depth - 1u];
    return (t->scr->flags & NX_SF_SHEET) && s_depth > 1u ? &s_lvl[s_depth - 2u] : t;
}

uint32_t nx_ui_now(void)
{
    return s_frame_ms;
}

uint8_t nx_ui_focus(void)
{
    return s_focus;
}

void nx_ui_set_focus(uint8_t ch)
{
    s_focus = (uint8_t)(ch & 1u);
}

int nx_ui_motion(void)
{
    return !nx_anim_reduced() && s_pw == PW_ACTIVE;
}

int nx_ui_locked(void)
{
    return s_locked;
}

unsigned nx_ui_depth(void)
{
    return s_depth;
}

const struct nx_scr *nx_ui_top(void)
{
    return s_depth ? s_lvl[s_depth - 1u].scr : 0;
}

static uint8_t theme_now(void)
{
    return (s_night_on || g_uiset.night) ? NX_THEME_NIGHT : g_uiset.theme;
}

/* ---- frame hook and layers ---- */

static void frame_start(void)
{
    s_frame_ms = nx_ms();
    g_vm_draw = g_vm;
    for (unsigned i = 0; i < s_depth; i++)
        memcpy(s_lvl[i].draw, s_lvl[i].live, s_lvl[i].scr->st_size);
    s_toast_draw = s_toast;
    s_hold_draw = s_hold;
    s_lock_draw = s_lock_card_t != 0;
    s_unlock_draw = s_unlock_t0;
    s_pw_draw = s_pw;
}

static void draw_screen(nx_gc *gc, void *ctx)
{
    (void)ctx;
    struct level *b = base_level();
    if (s_pw_draw == PW_AMBIENT) {
        nx_ambient_draw(gc);
        return;
    }
    if (!b)
        return;
    b->scr->draw(gc, b->draw);
    if (b->scr->hint != NX_NONE)
        nx_comp_hints(gc, b->scr->hint);
}

static void draw_sheet(nx_gc *gc, void *ctx)
{
    (void)ctx;
    struct level *t = top_level();
    if (t && (t->scr->flags & NX_SF_SHEET) && s_depth > 1u) {
        t->scr->draw(gc, t->draw);
        if (t->scr->hint != NX_NONE)
            nx_comp_hints(gc, t->scr->hint);
    }
}

static void draw_toast(nx_gc *gc, void *ctx)
{
    (void)ctx;
    nx_rect r = TOAST_AREA;
    nx_rrect(gc, r, 10, nx_col(gc, NX_PAL_SURFACE2));
    nx_rrect_line(gc, r, 10, 1, nx_col(gc, NX_PAL_OUTLINE));
    int16_t x = 24;
    if (s_toast_draw.icon != 0xFFu) {
        nx_icon(gc, &nx_icons20, s_toast_draw.icon, 22, 12, nx_col(gc, s_toast_draw.tok));
        x = 50;
    }
    nx_text_fit(gc, &nx_font_body16, x, 12, (int16_t)(r.x1 - 12 - x), s_toast_draw.text, nx_col(gc, NX_PAL_TEXT1));
}

static void draw_overlay(nx_gc *gc, void *ctx)
{
    (void)ctx;
    uint32_t now = s_frame_ms;
    if (s_lock_draw) {   /* key-lock card (ui.md W21) */
        nx_rect r = { 48, 60, 272, 180 };
        nx_card(gc, r);
        nx_icon(gc, &nx_icons32, NX_I32_LOCK, 144, 72, nx_col(gc, NX_PAL_TEXT1));
        nx_text_c(gc, &nx_font_body16, 160, 110, nx_str(NX_S_LOCK_LOCKED), nx_col(gc, NX_PAL_TEXT1));
        nx_text_c(gc, &nx_font_cap13, 160, 138, nx_str(NX_S_LOCK_UNLOCK_HINT), nx_col(gc, NX_PAL_TEXT2));
        if (s_unlock_draw) {
            uint32_t held = now - s_unlock_draw;
            uint16_t f = held >= LOCK_HOLD_MS ? 65535u : (uint16_t)(held * 65535u / LOCK_HOLD_MS);
            nx_hold_ring(gc, 160, 166, 8, 3, f, NX_PAL_FOCUS);
        }
        return;
    }
    if (s_hold_draw.on) {   /* hold ring: POWER, STOP-hold (ui.md W22) */
        uint32_t held = now - s_hold_draw.t0;
        uint16_t f = held <= s_hold_draw.from ? 0u
                   : held >= s_hold_draw.to ? 65535u
                   : (uint16_t)((held - s_hold_draw.from) * 65535u / (uint32_t)(s_hold_draw.to - s_hold_draw.from));
        nx_rect r = { 28, 50, 292, 190 };
        nx_card(gc, r);
        nx_hold_ring(gc, 160, 102, 32, 5, f, NX_PAL_FOCUS);
        if (s_hold_draw.icon & NX_UI_ICON20)
            nx_icon(gc, &nx_icons20, (uint8_t)(s_hold_draw.icon & 0x7Fu), 150, 92, nx_col(gc, NX_PAL_TEXT1));
        else
            nx_icon(gc, &nx_icons32, s_hold_draw.icon, 144, 86, nx_col(gc, NX_PAL_TEXT1));
        nx_text_c(gc, &nx_font_body16, 160, 148, nx_str(s_hold_draw.sid), nx_col(gc, NX_PAL_TEXT1));
        if (s_hold_draw.sid2 != NO_SID)
            nx_text_c(gc, &nx_font_cap13, 160, 168, nx_str(s_hold_draw.sid2), nx_col(gc, NX_PAL_TEXT2));
    }
}

/* Scrim while a sheet, a card or a ring covers the screen. */
static void update_scrim(void)
{
    struct level *t = top_level();
    uint8_t want = (t && (t->scr->flags & NX_SF_SHEET) && s_depth > 1u) || s_lock_card_t || s_hold.on;
    if (want != s_scrim) {
        s_scrim = want;
        nx_render_scrim(want ? NX_SCRIM_LEVEL : 0u);
        if (!nx_trans_active())
            nx_inval_all();
    }
}

/* ---- transitions ---- */

static void transition(uint8_t tr, const struct nx_scr *s)
{
    if (nx_trans_active() && !s_blocking)
        nx_trans_cancel();   /* a toast slide gives way */
    int ok = 1;
    s_blocking = 1;
    switch (tr) {
    case NX_TR_PUSH:
        ok = nx_trans_push(NX_PUSH_LEFT);
        break;
    case NX_TR_BACK:
        ok = nx_trans_push(NX_PUSH_RIGHT);
        break;
    case NX_TR_DIP:
        ok = nx_trans_dip();
        break;
    case NX_TR_SHEET:
        ok = nx_trans_reveal(NX_LAYER_SHEET, (nx_rect){ 0, (int16_t)(NX_LCD_H - s->sheet_h), NX_LCD_W, NX_LCD_H });
        break;
    default:
        ok = 0;
        break;
    }
    if (!ok) {
        s_blocking = 0;
        nx_inval_all();
    }
}

static void enter_level(struct level *l, const struct nx_scr *s, uint32_t arg)
{
    l->scr = s;
    memset(l->live, 0, sizeof l->live);
    if (s->enter)
        s->enter(l->live, arg);
    memcpy(l->draw, l->live, s->st_size);
}

void nx_ui_push(const struct nx_scr *s, uint32_t arg, uint8_t tr)
{
    if (s_depth == NX_UI_DEPTH)
        s_depth--;   /* full: the new screen replaces the top */
    enter_level(&s_lvl[s_depth++], s, arg);
    update_scrim();
    transition(tr, s);
}

void nx_ui_pop(uint8_t tr)
{
    if (s_depth <= 1u)
        return;
    s_depth--;
    update_scrim();
    transition(tr, top_level()->scr);
}

void nx_ui_reset(const struct nx_scr *s, uint32_t arg, uint8_t tr)
{
    s_depth = 0;
    nx_ui_push(s, arg, tr);
}

/* ---- toasts ---- */

void nx_ui_toast_s(uint8_t icon, uint8_t tok, uint16_t sid, const char *a0, const char *a1)
{
    if (s_bye)
        return;
    if (s_toastn == TOASTQ) {   /* full: the oldest waiting one goes */
        memmove(&s_toastq[0], &s_toastq[1], sizeof s_toastq[0] * (TOASTQ - 1u));
        s_toastn--;
    }
    struct toast *t = &s_toastq[s_toastn++];
    t->icon = icon;
    t->tok = tok;
    const char *args[2] = { a0, a1 };
    nx_str_format(t->text, sizeof t->text, sid, args, 2);
}

static void toast_task(uint32_t now)
{
    switch (s_toast_phase) {
    case 0:
        if (!s_toastn || nx_trans_active())
            return;
        s_toast = s_toastq[0];
        memmove(&s_toastq[0], &s_toastq[1], sizeof s_toastq[0] * (TOASTQ - 1u));
        s_toastn--;
        nx_render_layer(NX_LAYER_TOAST, draw_toast, 0);
        nx_trans_slide(NX_LAYER_TOAST, TOAST_AREA, -44, 0, TOAST_IN_MS);
        s_toast_phase = 1;
        s_toast_t = now;
        break;
    case 1:
        if (nx_trans_active())
            return;
        s_toast_phase = 2;
        s_toast_t = now;
        break;
    case 2:
        /* a waiting toast shortens the one shown to 1.5 s */
        if (now - s_toast_t < (s_toastn ? 1500u : TOAST_MS) || nx_trans_active())
            return;
        nx_trans_slide(NX_LAYER_TOAST, TOAST_AREA, 0, -44, TOAST_OUT_MS);
        s_toast_phase = 3;
        break;
    default:
        if (nx_trans_active())
            return;
        nx_render_layer(NX_LAYER_TOAST, 0, 0);
        nx_render_layer_offset(NX_LAYER_TOAST, 0, 0);
        nx_inval((nx_rect){ 12, -40, 308, 40 });
        s_toast_phase = 0;
        break;
    }
}

/* ---- overlays ---- */

void nx_ui_hold_show(uint16_t sid, uint8_t icon, uint32_t t0, uint16_t from_ms, uint16_t to_ms)
{
    int was = s_hold.on;
    s_hold.sid = sid;
    s_hold.sid2 = NO_SID;
    s_hold.icon = icon;
    s_hold.t0 = t0;
    s_hold.from = from_ms;
    s_hold.to = to_ms;
    s_hold.on = 1;
    update_scrim();
    if (was)
        nx_inval((nx_rect){ 28, 50, 292, 190 });
}

void nx_ui_hold_hide(void)
{
    if (!s_hold.on)
        return;
    s_hold.on = 0;
    update_scrim();
}

/* ---- settings ---- */

void nx_ui_apply(void)
{
    if (theme_now() != nx_render_theme_get())
        nx_render_theme(theme_now());
    nx_anim_set_reduced(g_uiset.motion);
    static const uint16_t k_first[3] = { 600, 400, 300 }, k_slow[3] = { 180, 110, 70 };
    if (g_uiset.origkeys)   /* the original's list repeat: after about 0.28 s, then about every 26 ms */
        nx_keys_set_repeat(280, 26);
    else
        nx_keys_set_repeat(k_first[g_uiset.repeat], k_slow[g_uiset.repeat]);
    if (s_pw == PW_ACTIVE && nx_uiset_level(&g_uiset) != nx_bl_user())
        nx_bl_set_user(nx_uiset_level(&g_uiset));
}

void nx_ui_settings_dirty(void)
{
    nx_uiset_to_blob(&g_uiset, nx_uc_settings_blob());
    s_save_due = 1;
}

void nx_ui_settings_flush(void)
{
    if (!s_save_due)
        return;
    int rc = nx_uc_settings_save();
    if (rc == NX_REFUSED_PC)
        return;   /* after PC mode */
    s_save_due = 0;
    if (rc != NX_OK)
        nx_ui_toast_s(NX_I20_ERROR, NX_PAL_ERROR, NX_S_ERR_FLASH, 0, 0);
}

/* ---- power states ---- */

static void wake(uint32_t now)
{
    s_input_t = now;
    switch (s_pw) {
    case PW_DIM:
    case PW_FADING:   /* the panel is still on: fade back */
        nx_bl_fade_to(nx_bl_user(), WAKE_FADE_MS);
        s_pw = PW_ACTIVE;
        break;
    case PW_OFF:      /* out of sleep, render in the dark, then DISPON and the fade (power_task) */
        s_pw = PW_WAKING;
        nx_lcd_power(NX_LCD_BLANK);
        nx_inval_all();
        break;
    case PW_AMBIENT:  /* the night view: back to the theme and the brightness */
        s_night_on = 0;
        nx_render_theme(theme_now());
        nx_bl_fade_to(nx_bl_user(), WAKE_FADE_MS);
        s_pw = PW_ACTIVE;
        break;
    default:
        break;
    }
}

void nx_ui_screen_off(void)
{
    if (s_pw == PW_OFF || s_pw == PW_FADING)
        return;
    if (s_pw == PW_AMBIENT) {
        s_night_on = 0;
        nx_render_theme(theme_now());
    }
    s_pw = PW_FADING;
    s_pw_t = nx_ms();
    nx_bl_fade_to(0, OFF_FADE_MS);
}

/* After the "off after" time: the screen off, or the night view (night palette, 3 % backlight,
 * both generators at a glance, one update per second). */
static void idle(uint32_t now)
{
    if (g_uiset.idle == NX_IDLE_AMBIENT) {
        s_pw = PW_AMBIENT;
        s_night_on = 1;
        nx_render_theme(NX_THEME_NIGHT);
        nx_bl_fade_to(AMBIENT_LEVEL, OFF_FADE_MS);
    } else {
        nx_ui_screen_off();
    }
    s_pw_t = now;
}

static void power_task(uint32_t now)
{
    if (nx_bl_w64_on_event() && s_pw != PW_ACTIVE)   /* the PC's w64 on: the screen comes back too */
        wake(now);
    const struct nx_scr *t = nx_ui_top();
    if (t && (t->flags & NX_SF_AWAKE))
        s_input_t = now;
    uint32_t idle_ms = now - s_input_t;
    uint32_t dim_ms = (uint32_t)k_nx_dim_s[g_uiset.dim] * 1000u, off_ms = (uint32_t)k_nx_off_s[g_uiset.off] * 1000u;
    switch (s_pw) {
    case PW_ACTIVE:
        if (dim_ms && idle_ms >= dim_ms) {
            s_pw = PW_DIM;
            uint8_t lv = (uint8_t)(nx_bl_user() / 3u);
            nx_bl_fade_to(lv < 16u ? 16u : lv, DIM_FADE_MS);
            nx_inval_all();   /* marquees and the scope stop */
        }
        break;
    case PW_DIM:
        if (off_ms && idle_ms >= dim_ms + off_ms)
            idle(now);
        break;
    case PW_FADING:
        if (now - s_pw_t >= OFF_FADE_MS) {
            nx_lcd_power(NX_LCD_SLEEP);
            s_pw = PW_OFF;
        }
        break;
    case PW_WAKING:
        if (nx_lcd_ready() && !nx_render_busy()) {
            nx_lcd_power(NX_LCD_ON);
            nx_bl_fade_to(nx_bl_user(), WAKE_FADE_MS);
            s_pw = PW_ACTIVE;
        }
        break;
    case PW_AMBIENT:
        if (g_vm.s.uptime_s != g_vm_seen.s.uptime_s)
            nx_inval_all();   /* one update per second */
        break;
    default:
        break;
    }
}

static void attention(uint32_t now)
{
    if (s_pw != PW_ACTIVE)
        wake(now);
    s_input_t = now;
}

/* ---- actions ---- */

/* No refusal of its own: nx_core_act refuses generator actions in PC mode (NX_REFUSED_PC)
 * before anything reaches a queue, and no screen of PC mode asks for one. */
int nx_ui_act(uint8_t type, uint8_t ch, uint16_t id, uint32_t v)
{
    struct nx_action a = { type, ch, id, v, 0 };
#ifdef HOST_TEST
    if (g_ui_actn < 16u)
        g_ui_actlog[g_ui_actn++] = a;
#endif
    int rc = nx_uc_act(&a);
    if (rc == NX_OK) {
        unsigned c0 = type == NXA_STOP_ALL ? 0u : ch, c1 = type == NXA_STOP_ALL ? 1u : ch;
        for (unsigned c = c0; c <= c1; c++) {
            s_finished[c] = 0;   /* any action on a generator ends its FINISHED state */
            if (type != NXA_LOOP && type != NXA_PAUSE)
                s_passes[c] = 0;
        }
        s_vm_t = nx_ms() - VM_MS;   /* rebuild the VM on this pass */
    } else if (rc == NX_BUSY && !g_vm.s.ready) {
        nx_ui_toast_s(NX_I20_HOURGLASS, NX_PAL_TEXT2, NX_S_UI_STARTING, 0, 0);
    }
    return rc;
}

/* ---- events and the VM ---- */

void nx_ui_post(uint8_t type, uint8_t ch, uint16_t a)
{
    uint8_t next = (uint8_t)((s_ev_head + 1u) % EVQ);
    if (next == s_ev_tail)
        return;   /* full: the snapshot still shows the state */
    s_evq[s_ev_head] = (struct nx_event){ type, ch, a };
    s_ev_head = next;
}

static void events(uint32_t now)
{
    while (s_ev_tail != s_ev_head) {
        struct nx_event e = s_evq[s_ev_tail];
        s_ev_tail = (uint8_t)((s_ev_tail + 1u) % EVQ);
        unsigned ch = e.ch & 1u;
        switch (e.type) {
        case NXE_FINISHED:
            if (e.a) {
                s_passes[ch]++;
            } else {
                char name[NX_UC_NAME_MAX + 1u];
                s_finished[ch] = 1;
                if (!nx_uc_name(g_vm.g[ch].slot, name)) {
                    char n[4];
                    nx_fmt_2d(n, g_vm.g[ch].slot);
                    const char *args[1] = { n };
                    nx_str_format(name, sizeof name, NX_S_LIB_SLOT_N, args, 1);
                }
                nx_ui_toast_s(NX_I20_CHECK_CIRCLE, NX_PAL_SUCCESS, NX_S_TOAST_FINISHED,
                              nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1), name);
                attention(now);
            }
            break;
        case NXE_CAL_VALUE:
            if (!g_vm.s.cal) {   /* factory port outside the calibration screen: a toast, no drawing */
                char i[4], v[8];
                nx_fmt_u(i, e.a);
                nx_fmt_u(v, (uint32_t)nx_uc_cal_value(e.a));
                nx_ui_toast_s(NX_I20_TUNE, NX_PAL_TEXT2, NX_S_CAL_FACTORY_VALUE, i, v);
            } else if (s_depth && top_level()->scr == &nx_scr_cal) {
                nx_scr_cal_factory(top_level()->live, e.a);
            }
            break;
        case NXE_CAL_ALL:
            nx_inval_all();
            break;
        case NXE_FLASH_ERROR:
            nx_ui_toast_s(NX_I20_ERROR, NX_PAL_ERROR, NX_S_ERR_FLASH, 0, 0);
            break;
#if NX_FEAT_TIMERS
        case NXE_TIMER_STOP: {
            char m[8];
            nx_fmt_u(m, e.a);
            nx_ui_toast_s(NX_I20_HOURGLASS, NX_PAL_TEXT2, NX_S_TOAST_TIMER_STOP, nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1), m);
            attention(now);
            break;
        }
        case NXE_ALL_DONE:
            s_done_due = 1;
            break;
#endif
#if NX_FEAT_TERRAIN
        case NXE_ALERT:
            s_alert_due = 1;
            break;
#endif
        case NXE_BFB_REFINE:   /* the scan screen over the dashboard of that generator */
            nx_ui_set_focus((uint8_t)ch);
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_NONE);
            nx_ui_push(&nx_scr_bfb, 0, NX_TR_PUSH);
            attention(now);
            break;
#if NX_FEAT_PRESET
        case NXE_NO_TABLE:
            nx_ui_toast_s(NX_I20_WARNING, NX_PAL_ERROR, NX_S_ERR_NO_RESPONSE, nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1), 0);
            attention(now);
            break;
#endif
#if NX_FEAT_SLOTS
        case NXE_SLOT_DONE: {   /* ch: 0 done, 1 failed, 2 no quiet moment; a: op << 8 | slot */
            static const uint16_t k_done[4] = { 0, NX_S_SLOT_COPIED, NX_S_SLOT_MOVED, NX_S_SLOT_ERASED };
            char n[4];
            nx_fmt_2d(n, e.a & 0xFFu);
            if (e.ch)
                nx_ui_toast_s(NX_I20_ERROR, NX_PAL_ERROR, e.ch == 2u ? NX_S_SLOT_NOT_QUIET : NX_S_SLOT_FAILED, 0, 0);
            else
                nx_ui_toast_s(NX_I20_CHECK_CIRCLE, NX_PAL_SUCCESS, k_done[(e.a >> 8) & 3u], n, 0);
            nx_inval_all();   /* the names in the lists */
            break;
        }
#endif
        default:
            break;
        }
    }
}

static void build_vm(void)
{
    nx_uc_vm(&g_vm);
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct nx_gen_vm *g = &g_vm.g[ch];
        if (g->state == NXG_RUNNING || g->state == NXG_NONE)
            s_finished[ch] = 0;
        if (s_finished[ch] && g->state == NXG_READY)
            g->state = NXG_FINISHED;
        g->passes = s_passes[ch];
    }
}

/* PC mode, calibration and link errors from the VM. */
static void modes(uint32_t now)
{
    if (g_vm.s.cal != g_vm_seen.s.cal) {
        nx_ui_reset(g_vm.s.cal ? &nx_scr_cal : &nx_scr_dash, 0, NX_TR_NONE);
        attention(now);
    }
    if (!g_vm.s.cal && g_vm.s.pc != g_vm_seen.s.pc) {
        nx_ui_hold_hide();
        /* a key that is down now started its gesture on the screen that goes: its hold and its
         * release do nothing on the new one (the original ignores a key in the pass that enters
         * or leaves PC mode). POWER keeps its meaning: screen off, and the core's power-off. */
        s_ignore = (uint8_t)(s_ignore | (nx_keys_down() & (uint8_t)~NX_KEY_POWER));
        nx_ui_reset(g_vm.s.pc ? &nx_scr_pc : &nx_scr_dash, 0, NX_TR_DIP);
        if (!g_vm.s.pc)
            nx_ui_toast_s(NX_I20_USB, NX_PAL_TEXT2, NX_S_PC_DISCONNECTED, 0, 0);
        attention(now);
    }
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (g_vm.g[ch].link_down && !g_vm_seen.g[ch].link_down && !g_vm.s.pc) {
            nx_ui_toast_s(NX_I20_WARNING, NX_PAL_ERROR, NX_S_ERR_NO_RESPONSE, nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1),
                          0);
            attention(now);
        }
    }
}

/* ---- keys ---- */

static void lock_card(uint32_t now)
{
    int was = s_lock_card_t != 0;
    s_lock_card_t = now ? now : 1u;
    if (!was)
        update_scrim();
}

void nx_ui_lock(int on)
{
    s_locked = (uint8_t)(on != 0);
    nx_ui_toast_s(on ? NX_I20_LOCK : NX_I20_LOCK_OPEN, NX_PAL_TEXT2, on ? NX_S_LOCK_LOCKED : NX_S_LOCK_UNLOCKED, 0, 0);
    nx_inval((nx_rect){ 0, 0, NX_LCD_W, NX_BAR_H });   /* the lock icon in the status bar */
}

/* 1 = consumed by the framework */
static int global_key(const struct nx_kev *e, uint32_t now)
{
    const struct nx_scr *t = nx_ui_top();
    uint8_t raw = t && (t->flags & NX_SF_RAW);
    if (e->keys == NX_KEY_POWER) {   /* the core powers off at NX_KEY_POWER_OFF_MS whatever happens here */
        if (e->type == NXK_PRESS)
            s_power_t0 = e->t ? e->t : 1u;
        if (e->type == NXK_RELEASE) {
            s_power_t0 = 0;
            nx_ui_hold_hide();
            if (e->n < NX_KEY_HOLD_MS && !raw) {
                if (g_uiset.origkeys) {   /* the original: on a generator's screen, stop both */
                    if (s_depth == 1u && nx_ui_top() == &nx_scr_dash && !g_vm.s.pc
                        && (g_vm.g[0].state != NXG_NONE || g_vm.g[1].state != NXG_NONE)
                        && nx_ui_act(NXA_STOP_ALL, 0, 0, 0) == NX_OK)
                        nx_ui_toast_s(NX_I20_STOP, NX_PAL_TEXT2, NX_S_TOAST_BOTH_STOPPED, 0, 0);
                } else if (s_pw == PW_ACTIVE || s_pw == PW_DIM) {
                    nx_ui_screen_off();
                } else {
                    wake(now);
                }
            }
        }
        return !raw;
    }
    if (raw)
        return 0;
    if (s_locked) {
        if (e->keys == (NX_KEY_START | NX_KEY_STOP) && e->type == NXK_CHORD)
            s_unlock_t0 = e->t;
        if (e->keys == (NX_KEY_START | NX_KEY_STOP) && e->type == NXK_RELEASE)
            s_unlock_t0 = 0;
        if (e->keys == (NX_KEY_START | NX_KEY_STOP) && e->type == NXK_LONG) {
            s_unlock_t0 = 0;
            s_lock_card_t = 0;
            update_scrim();
            nx_ui_lock(0);
            s_ignore = NX_KEY_START | NX_KEY_STOP;
        } else if (e->type == NXK_PRESS || e->type == NXK_CHORD) {
            lock_card(now);
        }
        return 1;
    }
    if (e->keys == (NX_KEY_G1 | NX_KEY_G2) && e->type == NXK_LONG && !g_vm.s.cal) {
        nx_ui_lock(1);
        s_ignore = NX_KEY_G1 | NX_KEY_G2;
        return 1;
    }
    return e->type == NXK_CHORD || (e->keys & (e->keys - 1u));   /* chords belong to the framework */
}

static void key_event(const struct nx_kev *e, uint32_t now)
{
    if (e->keys & s_ignore) {   /* its gesture started on another screen */
        if (e->type == NXK_RELEASE)
            s_ignore &= (uint8_t)~e->keys;
        return;
    }
    if (s_pw == PW_OFF || s_pw == PW_FADING || s_pw == PW_WAKING || s_pw == PW_AMBIENT) {
        if (e->type == NXK_PRESS || e->type == NXK_CHORD) {   /* the first key only wakes */
            s_ignore = (uint8_t)(s_ignore | e->keys);
            if (e->keys == NX_KEY_POWER)
                s_power_t0 = 0;
            wake(now);
        }
        return;
    }
    if (s_pw == PW_DIM)
        wake(now);
    s_input_t = now;
    int any = s_card == CARD_DONE || s_card == CARD_ALERT;
    if (s_card && (e->keys != NX_KEY_POWER || any)) {   /* resume: START / STOP only; power-off, set end: any key */
        if (e->type == NXK_PRESS || e->type == NXK_CHORD) {
            s_ignore = (uint8_t)(s_ignore | e->keys);
            if (any || e->keys == NX_KEY_START || e->keys == NX_KEY_STOP)
                card_answer(s_card != CARD_DONE && e->keys == NX_KEY_START);
        }
        return;
    }
    struct level *t = top_level();
    if (t && (t->scr->flags & NX_SF_PASS) && (e->type == NXK_PRESS || e->type == NXK_CHORD)) {
        t->scr->key(t->live, e);   /* the first-boot key guide: seen, closed, and the key acts below */
        nx_ui_pop(NX_TR_NONE);
    }
    if (global_key(e, now))
        return;
    t = top_level();
    if (!t || !t->scr->key)
        return;
    const struct nx_scr *before = t->scr;
    unsigned depth = s_depth;
    t->scr->key(t->live, e);
    if ((nx_ui_top() != before || s_depth != depth) && (e->type == NXK_PRESS || e->type == NXK_HOLD))
        s_ignore = (uint8_t)(s_ignore | e->keys);   /* its release belongs to the screen it left */
}

static void keys_task(uint32_t now)
{
    const struct nx_scr *t = nx_ui_top();
    uint8_t raw = t && (t->flags & NX_SF_RAW);
    nx_keys_config(t ? t->repeat : 0u, raw ? 0u : (NX_KEY_G1 | NX_KEY_G2));
    if (s_blocking && nx_trans_active())
        return;   /* a push, dip or reveal: keys wait in the queue */
    s_blocking = 0;
    struct nx_kev e;
    while (nx_keys_pop(&e)) {
        key_event(&e, now);
        if (s_blocking)
            break;   /* a transition started: the rest waits for it */
    }
}

/* Rings of held keys, the lock card's time, auto-lock. */
static void hold_task(uint32_t now)
{
    if (s_power_t0 && (nx_keys_down() & NX_KEY_POWER) && now - s_power_t0 >= POWER_RING_MS && !s_hold.on
        && s_pw != PW_OFF && !g_vm.s.cal)
        nx_ui_hold_show(NX_S_POWER_HOLD, NX_I32_POWER, s_power_t0, POWER_RING_MS, NX_KEY_POWER_OFF_MS);
    if (s_hold.on)
        nx_inval((nx_rect){ 124, 66, 196, 138 });   /* the ring fills */
    if (s_lock_card_t) {
        if (s_unlock_t0)
            nx_inval((nx_rect){ 148, 154, 172, 178 });
        if (now - s_lock_card_t >= LOCK_CARD_MS && !s_unlock_t0) {
            s_lock_card_t = 0;
            update_scrim();
        }
    }
    if (g_uiset.autolock && !s_locked && !g_vm.s.pc && !g_vm.s.cal && now - s_input_t >= AUTOLOCK_MS
        && (g_vm.g[0].state == NXG_RUNNING || g_vm.g[1].state == NXG_RUNNING))
        nx_ui_lock(1);
}

/* The offer cards: a card with a ring over the screen that answers "yes" by itself once full.
 * Resume after a power cut and the auto-start at power-on (nx_resume.h), on the dashboard: START
 * resumes / starts, STOP discards. Power off when all finished (nx_timer.h): the full ring powers
 * off, any key cancels. */
static void card_answer(int yes)
{
    uint8_t card = s_card;
    s_card = 0;
    nx_ui_hold_hide();
    if (card == CARD_RESUME || card == CARD_AUTO) {
        unsigned offer = nx_uc_resume_offer();
        nx_uc_resume_answer(yes);
        if (yes && offer == 2u) {   /* only G2 comes back: show G2 */
            nx_ui_set_focus(1);
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_DIP);
        }
    } else if (card == CARD_DONE && yes) {
        struct nx_action a = { NXA_POWER_OFF, 0, 0, 0, 0 };
        nx_uc_act(&a);
    }
}

static void card_task(uint32_t now)
{
    static const struct {
        uint16_t sid, sid2;
        uint8_t icon;
        uint16_t ms;
    } k_card[5] = {
        { 0, 0, 0, 0 },
        { NX_S_RESUME_ASK, NX_S_RESUME_KEYS, NX_UI_ICON20 | NX_I20_PLAY, RESUME_MS },
        { NX_S_DONE_TITLE, NX_S_DONE_KEYS, NX_I32_POWER, NX_DONE_OFF_MS },
        { NX_S_AUTO_ASK, NX_S_AUTO_KEYS, NX_UI_ICON20 | NX_I20_PLAY, NX_AUTO_MS },
        { NX_S_TERRAIN_DONE, NX_S_TERRAIN_DONE_KEYS, NX_UI_ICON20 | NX_I20_CHECK_CIRCLE, ALERT_MS },
    };
    int busy = g_vm.g[0].state == NXG_RUNNING || g_vm.g[1].state == NXG_RUNNING;
    if (s_card) {
        int gone = s_card == CARD_DONE ? g_vm.s.pc || busy : s_card == CARD_ALERT ? g_vm.s.pc : !nx_uc_resume_offer();
        if (s_bye || gone) {
            s_card = 0;   /* answered elsewhere, PC mode, or a program runs again */
            nx_ui_hold_hide();
        } else if (now - s_card_t0 >= k_card[s_card].ms) {
            card_answer(1);
        } else if (s_card == CARD_ALERT && s_pulse < 7u && (now - s_card_t0) / ALERT_PULSE_MS >= s_pulse) {
            /* no buzzer: the backlight pulses three times */
            s_pulse++;
            nx_bl_fade_to(s_pulse & 1u && s_pulse < 7u ? (uint8_t)(nx_bl_user() / 4u + 1u) : nx_bl_user(),
                          ALERT_PULSE_MS / 2u);
        }
        return;
    }
    if (s_bye || s_hold.on)
        return;
    uint8_t card = CARD_NONE;
    if (nx_uc_resume_offer()) {
        if (s_depth == 1u && nx_ui_top() == &nx_scr_dash)
            card = nx_uc_resume_auto() ? CARD_AUTO : CARD_RESUME;   /* after the splash, on the dashboard */
    } else if (s_done_due) {
        s_done_due = 0;
        if (!g_vm.s.pc && !busy) {
            card = CARD_DONE;
            wake(now);
        }
    } else if (s_alert_due) {
        s_alert_due = 0;
        if (!g_vm.s.pc) {
            card = CARD_ALERT;
            s_pulse = 0;
            wake(now);
        }
    }
    if (!card)
        return;
    s_card = card;
    s_card_t0 = now ? now : 1u;
    nx_ui_hold_show(k_card[card].sid, k_card[card].icon, now, 0, k_card[card].ms);
    s_hold.sid2 = k_card[card].sid2;
}

/* ---- nx_ui.h ---- */

void nx_ui_init(void)
{
    memset(s_lvl, 0, sizeof s_lvl);
    s_depth = s_focus = 0;
    s_ev_head = s_ev_tail = 0;
    s_finished[0] = s_finished[1] = 0;
    s_passes[0] = s_passes[1] = 0;
    s_pw = PW_ACTIVE;
    s_night_on = s_ignore = s_blocking = s_scrim = s_locked = s_save_due = 0;
    s_lock_card_t = s_unlock_t0 = s_power_t0 = s_card_t0 = 0;
    s_card = s_done_due = s_alert_due = 0;
    s_bye = s_bye_dark = s_bye_faded = s_bye_slept = 0;
    s_toastn = s_toast_phase = 0;
    memset(&s_hold, 0, sizeof s_hold);
    s_input_t = nx_ms();
#ifdef HOST_TEST
    g_ui_actn = 0;
#endif
    nx_scr_lib_boot();
    nx_scr_menu_boot();
    nx_render_init();
    nx_render_on_frame(frame_start);
    nx_render_layer(NX_LAYER_SCREEN, draw_screen, 0);
    nx_render_layer(NX_LAYER_SHEET, draw_sheet, 0);
    nx_render_layer(NX_LAYER_OVERLAY, draw_overlay, 0);
    nx_uiset_from_blob(&g_uiset, nx_uc_settings_blob());
    nx_ui_apply();
    nx_bl_set_user(nx_uiset_level(&g_uiset));
    build_vm();
    g_vm_seen = g_vm;
    s_vm_t = nx_ms();
    if (g_vm.s.cal || g_vm.s.pc) {   /* started in one of the core's modes */
        nx_ui_reset(g_vm.s.cal ? &nx_scr_cal : &nx_scr_pc, 0, NX_TR_NONE);
    } else {
        nx_ui_reset(&nx_scr_dash, 0, NX_TR_NONE);
        if (g_uiset.splash != NX_SPLASH_OFF)
            nx_ui_push(&nx_scr_splash, g_uiset.splash, NX_TR_NONE);   /* it pushes the guide when done */
        else if (!g_uiset.guide)
            nx_ui_push(&nx_scr_guide_boot, 0, NX_TR_NONE);
    }
    nx_render_flush();
}

void nx_ui_tick(uint32_t now_ms)
{
    if (s_bye)
        return;
    events(now_ms);
    int frame = now_ms - s_vm_t >= VM_MS;
    if (frame) {
        s_vm_t = now_ms;
        build_vm();
        modes(now_ms);
    }
    keys_task(now_ms);   /* keys act at once; the rest runs at most every VM_MS (100 per second) */
    if (!frame)
        return;
    hold_task(now_ms);
    card_task(now_ms);
    power_task(now_ms);
    toast_task(now_ms);
    /* One write once the settings screens are closed and nothing else is going on: the write
     * blocks the loop (up to ~30 ms for the first one), so it waits for a quiet moment. */
    if (s_save_due && s_depth == 1u && !g_vm.s.pc && now_ms - s_input_t >= SAVE_IDLE_MS && nx_uc_quiet(SAVE_QUIET_MS))
        nx_ui_settings_flush();
    struct level *b = base_level(), *t = top_level();
    if (b && b->scr->tick)
        b->scr->tick(b->live, now_ms);
    if (t && t != b && t->scr->tick)
        t->scr->tick(t->live, now_ms);
    g_vm_seen = g_vm;
}

int nx_ui_render_step(uint32_t budget_us)
{
    if (s_bye)
        return 0;
    return nx_render_step(budget_us);
}

int nx_ui_busy(void)
{
    return !s_bye && (nx_render_busy() || s_ev_tail != s_ev_head || s_toast_phase || s_toastn || s_hold.on
                      || s_lock_card_t || memcmp(&g_vm, &g_vm_seen, sizeof g_vm) != 0);
}

void nx_ui_cal_screen(void)
{
    build_vm();
    g_vm.s.cal = 1;
    g_vm_seen = g_vm;
    nx_ui_reset(&nx_scr_cal, 0, NX_TR_NONE);
    nx_bl_set_user(nx_uiset_level(&g_uiset));
    nx_render_flush();   /* the boot runs before the loop: draw it all now */
}

void nx_ui_goodbye(void)
{
    s_bye = 1;
    nx_bl_host_release();   /* as the original, the goodbye is lit even after a w64 off */
    if (nx_trans_active())
        nx_trans_cancel();
    s_bye_dark = s_pw != PW_ACTIVE && s_pw != PW_DIM;   /* switched off: it stays dark */
    if (s_save_due && !g_vm.s.pc) {   /* the last chance to keep a changed setting */
        nx_uiset_to_blob(&g_uiset, nx_uc_settings_blob());
        nx_uc_settings_save();
        s_save_due = 0;
    }
    if (s_bye_dark)
        return;
    s_hold.on = 0;
    s_lock_card_t = 0;
    s_toast_phase = 0;
    s_toastn = 0;
    nx_render_layer(NX_LAYER_TOAST, 0, 0);
    s_scrim = 0;
    nx_render_scrim(0);
    s_depth = 0;
    enter_level(&s_lvl[s_depth++], &nx_scr_bye, nx_ms());
    nx_inval_all();
    nx_render_flush();                 /* bounded; the core waits NX_GOODBYE_MS afterwards */
    nx_bl_fade_to(nx_bl_user(), 0);    /* lit, as in the original, until its last 300 ms */
}

void nx_ui_goodbye_step(uint32_t ms_left)
{
    uint32_t now = nx_ms();
    if (s_bye_dark || s_bye_slept || now == s_bye_t)
        return;   /* at most once per millisecond: the core calls this in a tight loop */
    s_bye_t = now;
    if (ms_left <= BYE_FADE_MS && !s_bye_faded) {
        s_bye_faded = 1;
        nx_bl_fade_to(0, BYE_FADE_MS - BYE_SLEEP_MS - 20u);
    }
    if (ms_left <= BYE_SLEEP_MS) {   /* DISPOFF + SLPIN before the core's PB2 signal */
        nx_lcd_power(NX_LCD_SLEEP);
        nx_lcd_poll();
        nx_lcd_power_step(now);
        s_bye_slept = nx_lcd_power_state() == NX_LCD_SLEEP;
        return;
    }
    struct level *t = top_level();
    if (t && t->scr->tick)
        t->scr->tick(t->live, now);
    nx_render_step(1000u);
}

/* factory.c: ':a0' (after clearing the screen) and ':e'..':l' */
void ui_cal_draw_values(void)
{
    nx_ui_post(NXE_CAL_ALL, 0, 0);
}

void ui_cal_draw_value(unsigned idx)
{
    nx_ui_post(NXE_CAL_VALUE, 0, (uint16_t)idx);
}
