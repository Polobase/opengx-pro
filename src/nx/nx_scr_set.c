/* Settings (docs/nx/design/ui.md W17): DOWN on the dashboard opens the root with Display, Keys &
 * lock, Motion, About (project, firmware version, panel type, "Support the project", licences) and
 * "Reset all settings" (hold-START confirmation), and the pages themselves. One screen, the page is its argument, so a page opens
 * on top of the root.
 *
 * Keys: UP/DOWN select; G1/G2 change a value or a switch (-/+); START opens a page, flips a switch,
 * steps a value, runs an action; STOP back (a changed setting is saved on the way out). */
#include <string.h>

#include "nx_comp.h"
#include "nx_config.h"
#include "nx_uifmt.h"

enum { P_ROOT, P_DISPLAY, P_KEYS, P_MOTION, P_ABOUT, P_TIMERS, P_N };
_Static_assert(P_TIMERS == NX_SET_TIMERS, "the menu opens the Timers page by number");

/* row ids; the root's links are in the order of the pages they open */
enum {
    R_DISPLAY, R_KEYS, R_MOTION, R_ABOUT, R_RESET,                     /* root */
    R_THEME, R_BRIGHT, R_DIM, R_OFF, R_IDLE, R_SPLASH,                 /* display */
    R_REPEAT, R_AUTOLOCK, R_RESUME, R_ORIG, R_GUIDE, R_LOCK_HOW, R_UNLOCK_HOW, /* keys */
    R_REDUCE, R_MARQUEE,                                               /* motion */
    R_STOP_G1, R_STOP_G2, R_OFF_DONE, R_AUTOSTART,                     /* timers (menu) */
    R_PROJECT, R_FW, R_AUTHOR, R_PANEL, R_SUPPORT,                     /* about; from R_LIC on: notes */
    R_LIC, R_FONT, R_FONT_C, R_FONT_L, R_ICONS, R_ICONS_C, R_ICONS_L, R_WARRANTY, R_VENDOR,
};

static const uint8_t k_rows_root[] = { R_DISPLAY, R_KEYS, R_MOTION, R_ABOUT, R_RESET };
static const uint8_t k_rows_display[] = { R_THEME, R_BRIGHT, R_DIM, R_OFF, R_IDLE, R_SPLASH };
static const uint8_t k_rows_keys[] = { R_REPEAT, R_AUTOLOCK, R_RESUME, R_ORIG, R_GUIDE, R_LOCK_HOW, R_UNLOCK_HOW };
static const uint8_t k_rows_motion[] = { R_REDUCE, R_MARQUEE };
static const uint8_t k_rows_timers[] = { R_STOP_G1, R_STOP_G2, R_OFF_DONE, R_AUTOSTART };
static const uint8_t k_rows_about[] = { R_PROJECT, R_FW,     R_AUTHOR, R_PANEL,
#if NX_FEAT_SUPPORT
                                        R_SUPPORT,
#endif
                                        R_LIC,     R_FONT,   R_FONT_C, R_FONT_L, R_ICONS, R_ICONS_C,
                                        R_ICONS_L, R_WARRANTY, R_VENDOR };

static const struct {
    const uint8_t *rows;
    uint8_t n;
    uint16_t title;
} k_pages[P_N] = {
    { k_rows_root, sizeof k_rows_root, NX_S_SET_TITLE },
    { k_rows_display, sizeof k_rows_display, NX_S_SET_DISPLAY },
    { k_rows_keys, sizeof k_rows_keys, NX_S_SET_KEYS },
    { k_rows_motion, sizeof k_rows_motion, NX_S_SET_MOTION },
    { k_rows_about, sizeof k_rows_about, NX_S_SET_ABOUT },
    { k_rows_timers, sizeof k_rows_timers, NX_S_SET_TIMERS },
};

/* the label of every row */
static const uint16_t k_label[] = {
    NX_S_SET_DISPLAY, NX_S_SET_KEYS, NX_S_SET_MOTION, NX_S_SET_ABOUT, NX_S_SET_RESET,
    NX_S_SET_THEME, NX_S_SET_BRIGHTNESS, NX_S_SET_DIM_AFTER, NX_S_SET_OFF_AFTER, NX_S_SET_IDLE, NX_S_SET_SPLASH,
    NX_S_SET_REPEAT, NX_S_SET_AUTO_LOCK, NX_S_SET_RESUME, NX_S_SET_ORIG_KEYS, NX_S_SET_KEY_GUIDE, NX_S_SET_LOCK_HOW,
    NX_S_SET_UNLOCK_HOW,
    NX_S_SET_REDUCE_MOTION, NX_S_SET_MARQUEE,
    NX_S_SET_STOP_G1, NX_S_SET_STOP_G2, NX_S_SET_OFF_DONE, NX_S_SET_AUTOSTART,
    NX_S_ABOUT_PROJECT, NX_S_ABOUT_FIRMWARE, NX_S_ABOUT_AUTHOR, NX_S_ABOUT_PANEL, NX_S_ABOUT_SUPPORT,
    NX_S_ABOUT_LICENCES, NX_S_ABOUT_FONT, NX_S_ABOUT_FONT_COPY, NX_S_ABOUT_FONT_LIC, NX_S_ABOUT_ICONS,
    NX_S_ABOUT_ICONS_COPY, NX_S_ABOUT_ICONS_LIC, NX_S_ABOUT_NO_WARRANTY, NX_S_ABOUT_NOT_VENDOR,
};
_Static_assert(sizeof k_label / sizeof k_label[0] == R_VENDOR + 1, "one label per settings row");

struct set {
    struct nx_list l;
    uint8_t page, changed;
    char panel;
};

static uint8_t row_id(const struct set *s, unsigned i)
{
    return k_pages[s->page].rows[i];
}

static uint8_t kind_of(uint8_t id)
{
    switch (id) {
    case R_DISPLAY:
    case R_KEYS:
    case R_MOTION:
    case R_ABOUT:
        return NX_ROW_LINK;
    case R_THEME:
    case R_DIM:
    case R_OFF:
    case R_IDLE:
    case R_SPLASH:
    case R_REPEAT:
    case R_STOP_G1:
    case R_STOP_G2:
        return NX_ROW_VALUE;
    case R_AUTOLOCK:
    case R_RESUME:
    case R_ORIG:
    case R_REDUCE:
    case R_MARQUEE:
    case R_OFF_DONE:
    case R_AUTOSTART:
        return NX_ROW_TOGGLE;
    case R_RESET:
    case R_GUIDE:
    case R_SUPPORT:
        return NX_ROW_ACTION;
    default:
        return NX_ROW_INFO;
    }
}

static const uint16_t k_theme_name[3] = { NX_S_THEME_DARK, NX_S_THEME_LIGHT, NX_S_THEME_HC };
static const uint16_t k_idle_name[2] = { NX_S_QUICK_SCREEN_OFF, NX_S_SET_IDLE_AMBIENT };
static const uint16_t k_splash_name[3] = { NX_S_COMMON_OFF, NX_S_SET_SPLASH_SHORT, NX_S_SET_SPLASH_FULL };
/* "stop after" choices in minutes (0 = off) */
static const uint16_t k_stop_min[] = { 0, 5, 15, 30, 45, 60, 90, 120, 180, 240 };
#define STOP_N (sizeof k_stop_min / sizeof k_stop_min[0])

static const uint16_t k_repeat_name[3] = { NX_S_SET_REPEAT_SLOW, NX_S_SET_REPEAT_NORMAL, NX_S_SET_REPEAT_FAST };

/* The value shown on the right of a row (empty: none). */
static void value_of(const struct set *s, uint8_t id, char *v)
{
    v[0] = 0;
    char *p;
    switch (id) {
    case R_DISPLAY:
        p = v + strlen(strcpy(v, nx_str(k_theme_name[g_uiset.theme])));
        *p++ = ' ';
        *p++ = (char)NX_CH_middot;
        *p++ = ' ';
        nx_fmt_pct(p, g_uiset.bright * 10u);
        break;
    case R_KEYS:
        strcpy(v, nx_str(k_repeat_name[g_uiset.repeat]));
        break;
    case R_MOTION:
        strcpy(v, nx_str(g_uiset.motion ? NX_S_SET_MOTION_REDUCED : NX_S_SET_MOTION_FULL));
        break;
    case R_THEME:
        strcpy(v, nx_str(k_theme_name[g_uiset.theme]));
        break;
    case R_BRIGHT:
        nx_fmt_pct(v, g_uiset.bright * 10u);
        break;
    case R_DIM:
        nx_fmt_delay(v, k_nx_dim_s[g_uiset.dim]);
        break;
    case R_OFF:
        nx_fmt_delay(v, k_nx_off_s[g_uiset.off]);
        break;
    case R_IDLE:
        strcpy(v, nx_str(k_idle_name[g_uiset.idle]));
        break;
    case R_SPLASH:
        strcpy(v, nx_str(k_splash_name[g_uiset.splash]));
        break;
    case R_REPEAT:
        strcpy(v, nx_str(k_repeat_name[g_uiset.repeat]));
        break;
    case R_STOP_G1:
    case R_STOP_G2: {
        unsigned m = nx_uc_opt(NX_OPT_STOP_G1 + (unsigned)(id - R_STOP_G1));
        if (!m)
            strcpy(v, nx_str(NX_S_COMMON_OFF));
        else
            nx_fmt_delay(v, m * 60u);
        break;
    }
    case R_PROJECT:
        strcpy(v, NX_PRODUCT);
        break;
    case R_FW:
        strcpy(v, NX_FW_VERSION);
        break;
    case R_AUTHOR:
        strcpy(v, NX_AUTHOR);
        break;
    case R_PANEL:
        p = v + strlen(strcpy(v, "ST7789 "));
        *p++ = (char)NX_CH_middot;
        *p++ = ' ';
        *p++ = s->panel;
        *p = 0;
        break;
    default:
        break;
    }
}

static void set_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel_ms;
    const struct set *s = ctx;
    uint8_t id = row_id(s, i);
    char v[40];
    value_of(s, id, v);
    if (id == R_BRIGHT) {
        nx_form_bar(gc, r, nx_str(k_label[id]), (uint16_t)(g_uiset.bright * 6553u), v, sel);
        return;
    }
    if (id == R_LOCK_HOW || id == R_UNLOCK_HOW || id >= R_LIC) {   /* notes in the caption font */
        nx_text_fit(gc, &nx_font_cap13, NX_M + 4, (int16_t)(r.y0 + 7), 290, nx_str(k_label[id]),
                    nx_col(gc, id == R_LIC ? NX_PAL_TEXT3 : id >= R_LIC ? NX_PAL_TEXT1 : NX_PAL_TEXT2));
        return;
    }
    int on = id == R_AUTOLOCK ? g_uiset.autolock : id == R_RESUME ? nx_uc_resume_on()
           : id == R_ORIG ? g_uiset.origkeys : id == R_REDUCE ? g_uiset.motion
           : id == R_OFF_DONE ? (int)nx_uc_opt(NX_OPT_OFF_DONE)
           : id == R_AUTOSTART ? (int)nx_uc_opt(NX_OPT_AUTOSTART) : g_uiset.marquee;
    nx_form_row(gc, r, nx_str(k_label[id]), v, kind_of(id), on, sel);
    if (id == R_RESET)
        nx_icon(gc, &nx_icons20, NX_I20_DELETE, 284, (int16_t)(r.y0 + 6), nx_col(gc, NX_PAL_WARNING));
}

static void set_enter(void *st, uint32_t arg)
{
    struct set *s = st;
    s->page = (uint8_t)(arg < P_N ? arg : P_ROOT);
    if (s->page == P_ABOUT)
        s->panel = nx_uc_panel();
    nx_list_init(&s->l, NX_BAR_H + 2, NX_ROW_H, 6, k_pages[s->page].n, 0, nx_ms());
}

static void set_draw(nx_gc *gc, const void *st)
{
    const struct set *s = st;
    nx_comp_title(gc, nx_str(k_pages[s->page].title), NX_LCD_W - NX_M, 1);
    nx_list_draw(gc, &s->l, NX_PAL_FOCUS, set_row, s);
}

static void do_reset(void)   /* the confirmation's yes: every UI setting to its default */
{
    uint8_t guide = g_uiset.guide;
    nx_uiset_defaults(&g_uiset);
    g_uiset.guide = guide;   /* the key guide was seen */
    nx_uc_resume_set(1);
    for (unsigned o = 0; o < NX_OPT_N; o++)
        nx_uc_opt_set(o, 0);
    nx_ui_apply();
    nx_ui_settings_dirty();
    nx_ui_toast_s(NX_I20_CHECK_CIRCLE, NX_PAL_SUCCESS, NX_S_SET_RESET_DONE, 0, 0);
    nx_inval_all();
}

/* -1 / +1 (G1 / G2) or START (d = 0) on a row; returns 1 when a setting changed. */
static int change(uint8_t id, int d)
{
    int step = d ? d : 1;
    switch (id) {
    case R_THEME:
        g_uiset.theme = (uint8_t)((g_uiset.theme + 3 + step) % 3);
        return 1;
    case R_BRIGHT:
        if ((int)g_uiset.bright + step < 1 || (int)g_uiset.bright + step > 10)
            return 0;
        g_uiset.bright = (uint8_t)(g_uiset.bright + step);
        return 1;
    case R_DIM:
        g_uiset.dim = (uint8_t)((g_uiset.dim + NX_DIM_N + (unsigned)step) % NX_DIM_N);
        return 1;
    case R_OFF:
        g_uiset.off = (uint8_t)((g_uiset.off + NX_OFF_N + (unsigned)step) % NX_OFF_N);
        return 1;
    case R_IDLE:
        g_uiset.idle ^= 1u;
        return 1;
    case R_SPLASH:
        g_uiset.splash = (uint8_t)((g_uiset.splash + 3 + step) % 3);
        return 1;
    case R_REPEAT:
        g_uiset.repeat = (uint8_t)((g_uiset.repeat + 3 + step) % 3);
        return 1;
    case R_AUTOLOCK:
        g_uiset.autolock ^= 1u;
        return 1;
    case R_ORIG:     /* the original's keys (POWER short stops both, START / LOOP, repeat) */
        g_uiset.origkeys ^= 1u;
        return 1;
    case R_RESUME:   /* the core's record; saved with the UI's settings */
        nx_uc_resume_set(!nx_uc_resume_on());
        return 1;
    case R_REDUCE:
        g_uiset.motion ^= 1u;
        return 1;
    case R_STOP_G1:
    case R_STOP_G2: {   /* the next choice up (G2, START) or down (G1), wrapping */
        unsigned o = NX_OPT_STOP_G1 + (unsigned)(id - R_STOP_G1), m = nx_uc_opt(o), i = 0;
        while (i < STOP_N - 1u && k_stop_min[i] < m)
            i++;
        i = step > 0 ? (k_stop_min[i] > m ? i : (i + 1u) % STOP_N) : (i + STOP_N - 1u) % STOP_N;
        nx_uc_opt_set(o, k_stop_min[i]);
        return 1;
    }
    case R_OFF_DONE:
    case R_AUTOSTART: {
        unsigned o = id == R_OFF_DONE ? NX_OPT_OFF_DONE : NX_OPT_AUTOSTART;
        nx_uc_opt_set(o, !nx_uc_opt(o));
        return 1;
    }
    case R_MARQUEE:
        g_uiset.marquee ^= 1u;
        return 1;
    default:
        return 0;
    }
}

static int set_key(void *st, const struct nx_kev *e)
{
    struct set *s = st;
    if (nx_list_key(&s->l, e, nx_ms()))
        return 1;
    if (e->type != NXK_PRESS && e->type != NXK_REPEAT)
        return 1;
    uint8_t id = row_id(s, s->l.sel);
    switch (e->keys) {
    case NX_KEY_STOP:
        if (e->type == NXK_PRESS) {
            if (s->changed)
                nx_ui_settings_dirty();
            nx_ui_pop(NX_TR_BACK);
        }
        return 1;
    case NX_KEY_START:
        if (e->type != NXK_PRESS)
            return 1;
        if (kind_of(id) == NX_ROW_LINK) {
            nx_ui_push(&nx_scr_set, (uint32_t)(id - R_DISPLAY + P_DISPLAY), NX_TR_PUSH);
            return 1;
        }
        if (id == R_RESET) {
            nx_ui_confirm(NX_S_CONFIRM_RESET, NX_S_SET_RESET_BODY, do_reset);
            return 1;
        }
        if (id == R_GUIDE) {
            nx_ui_push(&nx_scr_guide, 1, NX_TR_DIP);
            return 1;
        }
#if NX_FEAT_SUPPORT
        if (id == R_SUPPORT) {
            nx_ui_push(&nx_scr_support, 0, NX_TR_PUSH);
            return 1;
        }
#endif
        break;
    case NX_KEY_G1:
    case NX_KEY_G2:
        if (kind_of(id) != NX_ROW_VALUE && kind_of(id) != NX_ROW_TOGGLE && id != R_BRIGHT)
            return 1;
        break;
    default:
        return 1;
    }
    int d = e->keys == NX_KEY_G1 ? -1 : e->keys == NX_KEY_G2 ? 1 : 0;
    if (change(id, d)) {
        s->changed = 1;
        nx_ui_apply();
        nx_ui_settings_dirty();
        nx_inval(nx_list_view(&s->l));
        if (id == R_THEME)
            nx_inval_all();
    }
    return 1;
}

static void set_tick(void *st, uint32_t now)
{
    nx_list_tick(&((struct set *)st)->l, now);
}

const struct nx_scr nx_scr_set = {
    set_enter, set_key, set_tick, set_draw, NX_S_HINT_SETTINGS, 0, NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2, 0,
    sizeof(struct set),
};
