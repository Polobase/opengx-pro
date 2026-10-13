/* Bottom sheets over the dashboard (docs/nx/design/ui.md W4, W5, W21): quick settings (UP), the
 * hold-START confirmation for destructive actions and the menu (DOWN).
 * Keys: UP/DOWN select; G1/G2 change a value or a switch (-/+); START flips a switch, steps a
 * value, runs an action; STOP closes (a changed setting is saved later, nx_ui_settings_dirty). */
#include "nx_comp.h"
#include "nx_config.h"
#include "nx_uifmt.h"

/* The sheet from under the status bar: its handle, the title, then the six rows down to the
 * key-hint bar (NX_HINT_Y). */
#define TITLE_H 34

#define QUICK_H 214
#define QUICK_Y0 (NX_LCD_H - QUICK_H)
#define QROW_H 26
_Static_assert(QUICK_Y0 + TITLE_H + 6 * QROW_H <= NX_HINT_Y, "quick settings end above the hint bar");

enum { Q_BRIGHT, Q_NIGHT, Q_LOCK, Q_OFF, Q_DIM, Q_MOTION, Q_N };

struct quick {
    struct nx_list l;
    uint8_t changed;
};

static void quick_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    (void)sel_ms;
    char v[16];
    switch (i) {
    case Q_BRIGHT:
        nx_fmt_pct(v, g_uiset.bright * 10u);
        nx_form_bar(gc, r, nx_str(NX_S_QUICK_BRIGHTNESS), (uint16_t)(g_uiset.bright * 6553u), v, sel);
        break;
    case Q_NIGHT:
        nx_form_row(gc, r, nx_str(NX_S_QUICK_NIGHT), 0, NX_ROW_TOGGLE, g_uiset.night, sel);
        break;
    case Q_LOCK:
        nx_form_row(gc, r, nx_str(NX_S_QUICK_KEY_LOCK), 0, NX_ROW_ACTION, 0, sel);
        break;
    case Q_OFF:
        nx_form_row(gc, r, nx_str(NX_S_QUICK_SCREEN_OFF), 0, NX_ROW_ACTION, 0, sel);
        break;
    case Q_DIM:
        nx_fmt_delay(v, k_nx_dim_s[g_uiset.dim]);
        nx_form_row(gc, r, nx_str(NX_S_QUICK_AUTO_DIM), v, NX_ROW_VALUE, 0, sel);
        break;
    default:
        nx_form_row(gc, r, nx_str(NX_S_SET_REDUCE_MOTION), 0, NX_ROW_TOGGLE, g_uiset.motion, sel);
        break;
    }
}

static void quick_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct quick *q = st;
    nx_list_init(&q->l, QUICK_Y0 + TITLE_H, QROW_H, Q_N, Q_N, 0, nx_ms());
}

static void quick_draw(nx_gc *gc, const void *st)
{
    const struct quick *q = st;
    nx_sheet_frame(gc, QUICK_Y0);
    /* centred under the handle: the title reads apart from the left-aligned rows in the same font */
    nx_text_c(gc, &nx_font_body16, 160, QUICK_Y0 + 12, nx_str(NX_S_QUICK_TITLE), nx_col(gc, NX_PAL_TEXT1));
    nx_list_draw(gc, &q->l, NX_PAL_FOCUS, quick_row, 0);
}

static void quick_close(struct quick *q)
{
    if (q->changed)
        nx_ui_settings_dirty();
    nx_ui_pop(NX_TR_NONE);
}

static int quick_key(void *st, const struct nx_kev *e)
{
    struct quick *q = st;
    if (nx_list_key(&q->l, e, nx_ms()))
        return 1;
    if (e->type != NXK_PRESS && e->type != NXK_REPEAT)
        return 1;
    int d = e->keys == NX_KEY_G1 ? -1 : e->keys == NX_KEY_G2 ? 1 : 0;
    switch (e->keys) {
    case NX_KEY_STOP:
        if (e->type == NXK_PRESS)
            quick_close(q);
        return 1;
    case NX_KEY_START:
    case NX_KEY_G1:
    case NX_KEY_G2:
        break;
    default:
        return 1;
    }
    int start = e->keys == NX_KEY_START && e->type == NXK_PRESS;
    switch (q->l.sel) {
    case Q_BRIGHT:
        if (d && (int)g_uiset.bright + d >= 1 && (int)g_uiset.bright + d <= 10)
            g_uiset.bright = (uint8_t)(g_uiset.bright + d);
        break;
    case Q_NIGHT:
        if (d || start)
            g_uiset.night ^= 1u;
        break;
    case Q_LOCK:
        if (start) {
            quick_close(q);
            nx_ui_lock(1);
        }
        return 1;
    case Q_OFF:
        if (start) {
            quick_close(q);
            nx_ui_screen_off();
        }
        return 1;
    case Q_DIM:
        if (start)
            d = 1;
        g_uiset.dim = (uint8_t)((g_uiset.dim + NX_DIM_N + (unsigned)d) % NX_DIM_N);
        break;
    default:
        if (d || start)
            g_uiset.motion ^= 1u;
        break;
    }
    q->changed = 1;
    nx_ui_apply();
    nx_inval(nx_list_row(&q->l, q->l.sel));
    return 1;
}

static void quick_tick(void *st, uint32_t now)
{
    nx_list_tick(&((struct quick *)st)->l, now);
}

const struct nx_scr nx_scr_quick = {
    quick_enter, quick_key, quick_tick, quick_draw, NX_S_HINT_QUICK, NX_SF_SHEET,
    NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2, QUICK_H, sizeof(struct quick),
};

/* ---- hold-START confirmation (W21): destructive actions only (Reset all settings) ---- */

#define CONF_H 150
#define CONF_Y0 (NX_LCD_H - CONF_H)
#define RING_X 290
#define RING_Y (CONF_Y0 + 104)
static const nx_rect k_ring = { RING_X - 20, RING_Y - 20, RING_X + 20, RING_Y + 20 };

struct confirm {
    uint32_t t0;   /* START down since, 0 = up */
};

static struct nx_confirm s_conf;   /* one confirmation at a time */

void nx_ui_confirm(uint16_t title, uint16_t body, void (*yes)(void))
{
    s_conf = (struct nx_confirm){ title, body, yes };
    nx_ui_push(&nx_scr_confirm, 0, NX_TR_SHEET);
}

static void conf_draw(nx_gc *gc, const void *st)
{
    const struct confirm *c = st;
    nx_sheet_frame(gc, CONF_Y0);
    nx_icon(gc, &nx_icons20, NX_I20_WARNING, NX_M + 4, CONF_Y0 + 20, nx_col(gc, NX_PAL_WARNING));
    nx_text_fit(gc, &nx_font_body16, NX_M + 32, CONF_Y0 + 20, 260, nx_str(s_conf.title), nx_col(gc, NX_PAL_TEXT1));
    nx_text_fit(gc, &nx_font_body16, NX_M + 4, CONF_Y0 + 52, 296, nx_str(s_conf.body), nx_col(gc, NX_PAL_TEXT2));
    uint32_t held = c->t0 ? nx_ui_now() - c->t0 : 0u;
    uint16_t f = held >= NX_CONFIRM_MS ? 65535u : (uint16_t)(held * 65535u / NX_CONFIRM_MS);
    nx_hold_ring(gc, RING_X, RING_Y, 14, 4, f, NX_PAL_WARNING);
    nx_text_fit(gc, &nx_font_cap13, NX_M + 4, RING_Y - 9, 250, nx_str(NX_S_CONFIRM_HOLD), nx_col(gc, NX_PAL_TEXT2));
}

static int conf_key(void *st, const struct nx_kev *e)
{
    struct confirm *c = st;
    if (e->keys == NX_KEY_START && e->type == NXK_PRESS)
        c->t0 = e->t ? e->t : 1u;
    if (e->keys == NX_KEY_START && e->type == NXK_RELEASE) {
        c->t0 = 0;
        nx_inval(k_ring);
    }
    if (e->keys == NX_KEY_STOP && e->type == NXK_PRESS)
        nx_ui_pop(NX_TR_NONE);
    return 1;
}

static void conf_tick(void *st, uint32_t now)
{
    struct confirm *c = st;
    if (!c->t0)
        return;
    nx_inval(k_ring);
    if ((nx_keys_down() & NX_KEY_START) && now - c->t0 >= NX_CONFIRM_MS) {
        void (*yes)(void) = s_conf.yes;
        c->t0 = 0;
        nx_ui_pop(NX_TR_NONE);
        yes();
    }
}

const struct nx_scr nx_scr_confirm = {
    0, conf_key, conf_tick, conf_draw, NX_NONE, NX_SF_SHEET, 0, CONF_H, sizeof(struct confirm),
};

/* ---- menu (W4): DOWN on the dashboard; a grid of three columns, G1/G2 move along a row,
 * UP/DOWN change the row, START opens, STOP closes. It shows the screens that exist in this
 * build and opens on the item used last (at power-on: Settings). ---- */

#define MENU_H 184
#define MENU_Y0 (NX_LCD_H - MENU_H)
#define MENU_COLS (MENU_N > 6u ? 4u : 3u)   /* 4 columns for more than 6 items */

struct menu_item {
    uint8_t icon;
    uint16_t label;
    const struct nx_scr *scr;   /* NULL: the program list of the focused generator */
    uint8_t arg;                /* the screen's argument */
};

static const struct menu_item k_menu[] = {
    { NX_I20_LIBRARY, NX_S_MENU_PROGRAMS, 0, 0 },
#if NX_FEAT_CHAINS
    { NX_I20_CHAIN, NX_S_MENU_CHAINS, &nx_scr_chains, 0 },
#endif
#if NX_FEAT_MANUAL
    { NX_I20_TUNE, NX_S_MENU_MANUAL, &nx_scr_manual, 0 },
#endif
#if NX_FEAT_TIMERS
    { NX_I20_TIMER, NX_S_MENU_TIMERS, &nx_scr_set, NX_SET_TIMERS },
#endif
#if NX_FEAT_HIST
    { NX_I20_HISTORY, NX_S_MENU_HISTORY, &nx_scr_hist, 0 },
#endif
#if NX_FEAT_DIAG
    { NX_I20_DIAG, NX_S_MENU_DIAG, &nx_scr_diag, 0 },
#endif
    { NX_I20_SETTINGS, NX_S_MENU_SETTINGS, &nx_scr_set, 0 },
};
#define MENU_N (sizeof k_menu / sizeof k_menu[0])

struct menu {
    uint8_t sel;
};

static uint8_t s_menu_last;

void nx_scr_menu_boot(void)
{
    s_menu_last = MENU_N - 1u;   /* Settings */
}

#ifdef HOST_TEST
void nx_scr_menu_preselect(const struct nx_scr *scr, uint8_t arg)
{
    for (unsigned i = 0; i < MENU_N; i++)
        if (k_menu[i].scr == scr && k_menu[i].arg == arg)
            s_menu_last = (uint8_t)i;
}
#endif

static nx_rect menu_cell(unsigned i)
{
    unsigned w = MENU_COLS == 4u ? 76u : 100u;   /* 4 x 76: "Diagnostics" (72 px) still fits */
    int16_t x = (int16_t)((MENU_COLS == 4u ? 5u : 6u) + (i % MENU_COLS) * (w + (MENU_COLS == 4u ? 2u : 3u))),
            y = (int16_t)(MENU_Y0 + TITLE_H + (i / MENU_COLS) * 60u);
    return (nx_rect){ x, y, (int16_t)(x + w), (int16_t)(y + 54) };
}

static void menu_enter(void *st, uint32_t arg)
{
    (void)arg;
    ((struct menu *)st)->sel = s_menu_last < MENU_N ? s_menu_last : 0u;
}

static void menu_draw(nx_gc *gc, const void *st)
{
    const struct menu *m = st;
    nx_sheet_frame(gc, MENU_Y0);
    nx_text_c(gc, &nx_font_body16, 160, MENU_Y0 + 12, nx_str(NX_S_MENU_TITLE), nx_col(gc, NX_PAL_TEXT1));
    for (unsigned i = 0; i < MENU_N; i++)
        nx_grid_cell(gc, menu_cell(i), k_menu[i].icon, nx_str(k_menu[i].label), i == m->sel);
}

static int menu_key(void *st, const struct nx_kev *e)
{
    struct menu *m = st;
    if (e->type != NXK_PRESS && e->type != NXK_REPEAT)
        return 1;
    unsigned old = m->sel, n = MENU_N, row = old / MENU_COLS, col = old % MENU_COLS;
    unsigned rows = (n + MENU_COLS - 1u) / MENU_COLS;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2: {   /* along the row, wrapping inside it */
        unsigned in_row = row + 1u < rows ? MENU_COLS : n - row * MENU_COLS;
        col = e->keys == NX_KEY_G2 ? (col + 1u) % in_row : (col + in_row - 1u) % in_row;
        m->sel = (uint8_t)(row * MENU_COLS + col);
        break;
    }
    case NX_KEY_UP:
    case NX_KEY_DOWN:
        row = e->keys == NX_KEY_DOWN ? (row + 1u) % rows : (row + rows - 1u) % rows;
        m->sel = (uint8_t)(row * MENU_COLS + col < n ? row * MENU_COLS + col : n - 1u);
        break;
    case NX_KEY_START:
        if (e->type != NXK_PRESS)
            return 1;
        s_menu_last = m->sel;
        nx_ui_pop(NX_TR_NONE);
        if (k_menu[m->sel].scr)
            nx_ui_push(k_menu[m->sel].scr, k_menu[m->sel].arg, NX_TR_PUSH);
        else
            nx_ui_push(&nx_scr_lib, nx_ui_focus(), NX_TR_PUSH);
        return 1;
    case NX_KEY_STOP:
        if (e->type == NXK_PRESS) {
            s_menu_last = m->sel;
            nx_ui_pop(NX_TR_NONE);
        }
        return 1;
    default:
        return 1;
    }
    if (m->sel != old) {
        nx_inval(menu_cell(old));
        nx_inval(menu_cell(m->sel));
    }
    return 1;
}

const struct nx_scr nx_scr_menu = {
    menu_enter, menu_key, 0, menu_draw, NX_S_HINT_MENU, NX_SF_SHEET, NX_KEY_G1 | NX_KEY_G2, MENU_H,
    sizeof(struct menu),
};
