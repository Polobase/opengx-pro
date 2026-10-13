/* Chains (menu > Chains, NX_FEAT_CHAINS): the list of chains and multi-day plans (NX_FEAT_PLANS)
 * and their editor.
 *
 * List keys: UP/DOWN select, G1/G2 the generator it plays on, START plays the chain (a plan: its
 * current day's chain; a built-in preset, NX_FEAT_PRESET: from its start; a Terrain protocol,
 * NX_FEAT_TERRAIN: from its position) there and shows that generator's dashboard (an empty one
 * opens the editor), LOOP edits (a Terrain protocol: its screen, nx_scr_terrain.c; a preset:
 * nothing), STOP back.
 * Editor keys: UP/DOWN select, G1/G2 change the selected program's slot (a plan: the day's
 * chain), START adds one after the selection (a copy of it), START held removes the selected
 * one, LOOP steps the repeat count (x1, 2, 3, 5, 10, until stopped; a plan: back to day 1), STOP
 * saves and goes back. */
#include "nx_config.h"

#if NX_FEAT_CHAINS
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };

#if NX_FEAT_PLANS
#define LIST_N (NX_CHAINS + NX_PLANS)   /* the chains, then the plans */
#define PLAN_ARG 0x100u                 /* the editor's argument for a plan */
#else
#define LIST_N NX_CHAINS
#endif

static unsigned presets_n(void)
{
#if NX_FEAT_PRESET
    return nx_uc_preset_count();
#else
    return 0;
#endif
}

static uint8_t rows_n(void)   /* the chains, the plans, the built-in presets, then the Terrain protocols */
{
#if NX_FEAT_TERRAIN
    return (uint8_t)(LIST_N + presets_n() + nx_uc_terrain_count());
#else
    return (uint8_t)(LIST_N + presets_n());
#endif
}

/* "x2" / "until stopped" */
static void repeat_text(char *out, uint8_t repeat)
{
    if (!repeat) {
        strcpy(out, nx_str(NX_S_CHAIN_FOREVER));
        return;
    }
    char n[4];
    const char *args[1] = { n };
    nx_fmt_u(n, repeat);
    nx_str_format(out, 16, NX_S_CHAIN_TIMES, args, 1);
}

/* ---- list ---- */

struct chains {
    struct nx_list l;
    uint8_t target;
};

#if NX_FEAT_PLANS
static void plan_row(nx_gc *gc, unsigned i, int16_t ty)
{
    const struct nx_plan *p = &nx_uc_plans()[i];
    char a[8], b[8], t[40];
    const char *args[2] = { a, b };
    nx_fmt_u(a, i + 1u);
    nx_str_format(t, sizeof t, NX_S_PLAN_NAME, args, 1);
    nx_text(gc, &nx_font_body16, NX_M + 4, ty, t, nx_col(gc, NX_PAL_TEXT1));
    nx_fmt_u(a, p->n);
    nx_fmt_u(b, p->day + 1u);
    if (!p->n)
        strcpy(t, nx_str(NX_S_CHAIN_EMPTY));
    else if (p->day >= p->n)
        strcpy(t, nx_str(NX_S_PLAN_COMPLETE));
    else
        nx_str_format(t, sizeof t, NX_S_PLAN_DAYS, args, 2);
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, (int16_t)(ty + 3), t, nx_col(gc, NX_PAL_TEXT2));
    for (unsigned ch = 0; ch < 2u; ch++)
        if (g_vm_draw.g[ch].plan == i + 1u)
            nx_icon(gc, &nx_icons20, NX_I20_PLAY, (int16_t)(96 + ch * 22), ty, nx_col(gc, nx_accent(ch)));
}
#endif

#if NX_FEAT_TERRAIN || NX_FEAT_PRESET
/* A built-in row: its state at the right, before it a play icon for each generator of `playing`
 * (bit ch); returns where the name has to end. */
static int16_t row_state(nx_gc *gc, int16_t ty, const char *state, unsigned playing)
{
    int16_t rx = nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, (int16_t)(ty + 3), state, nx_col(gc, NX_PAL_TEXT2));
    for (unsigned ch = 0; ch < 2u; ch++)
        if (playing & (1u << ch)) {
            rx = (int16_t)(rx - 24);
            nx_icon(gc, &nx_icons20, NX_I20_PLAY, rx, ty, nx_col(gc, nx_accent(ch)));
        }
    return (int16_t)(rx - NX_M - 12);
}
#endif

#if NX_FEAT_TERRAIN
/* "Terrain · Remote   Day 3/11", the generators that play it before the day */
static void terrain_row(nx_gc *gc, unsigned id, int16_t ty)
{
    struct nx_tview v;
    nx_uc_terrain_view(id, &v);
    char a[8], b[8], t[48];
    const char *args[2] = { a, b };
    if (v.item >= v.n) {
        strcpy(t, nx_str(NX_S_TERRAIN_COMPLETE));
    } else {
        nx_fmt_u(a, v.day);
        nx_fmt_u(b, v.days);
        nx_str_format(t, sizeof t, NX_S_PLAN_STATUS, args, 2);
    }
    int16_t w = row_state(gc, ty, t, v.playing);
    args[0] = v.name;
    nx_str_format(t, sizeof t, v.kind == NX_TP_REMOTE ? NX_S_TERRAIN_REMOTE : NX_S_TERRAIN_CONTACT, args, 1);
    nx_text_fit(gc, &nx_font_body16, NX_M + 4, ty, w, t, nx_col(gc, NX_PAL_TEXT1));
}
#endif

#if NX_FEAT_PRESET
/* Its name (they are long: the row is theirs), then a play icon for each generator it is loaded on */
static void preset_row(nx_gc *gc, unsigned n, int16_t ty)
{
    char t[NX_UC_NAME_MAX + 1u];
    int16_t w = row_state(gc, ty, "", (g_vm_draw.g[0].slot == NX_UC_PRESET + n ? 1u : 0u)
                                      | (g_vm_draw.g[1].slot == NX_UC_PRESET + n ? 2u : 0u));
    nx_uc_name(NX_UC_PRESET + n, t);
    nx_text_fit(gc, &nx_font_body16, NX_M + 4, ty, w, t, nx_col(gc, NX_PAL_TEXT1));
}
#endif

static void chains_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    (void)sel;
    (void)sel_ms;
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2);
#if NX_FEAT_PRESET
    if (i >= LIST_N && i < LIST_N + presets_n()) {
        preset_row(gc, i - LIST_N, ty);
        return;
    }
#endif
#if NX_FEAT_TERRAIN
    if (i >= LIST_N) {
        terrain_row(gc, i - LIST_N - presets_n() + 1u, ty);
        return;
    }
#endif
#if NX_FEAT_PLANS
    if (i >= NX_CHAINS) {
        plan_row(gc, i - NX_CHAINS, ty);
        return;
    }
#endif
    const struct nx_chain *c = &nx_uc_chains()[i];
    char a[8], t[40];
    const char *args[1] = { a };
    nx_fmt_u(a, i + 1u);
    nx_str_format(t, sizeof t, NX_S_CHAIN_NAME, args, 1);
    nx_text(gc, &nx_font_body16, NX_M + 4, ty, t, nx_col(gc, NX_PAL_TEXT1));
    if (!c->n) {
        strcpy(t, nx_str(NX_S_CHAIN_EMPTY));
    } else {
        nx_fmt_u(a, c->n);
        size_t n = nx_str_format(t, sizeof t, NX_S_CHAIN_PROGRAMS, args, 1);
        t[n++] = ' ';
        t[n++] = (char)NX_CH_middot;
        t[n++] = ' ';
        repeat_text(t + n, c->repeat);
    }
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, (int16_t)(ty + 3), t, nx_col(gc, NX_PAL_TEXT2));
    for (unsigned ch = 0; ch < 2u; ch++)   /* the generators that play it */
        if (g_vm_draw.g[ch].chain == i + 1u)
            nx_icon(gc, &nx_icons20, NX_I20_PLAY, (int16_t)(120 + ch * 22), ty, nx_col(gc, nx_accent(ch)));
}

static void chains_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct chains *c = st;
    c->target = (uint8_t)nx_ui_focus();
    nx_list_init(&c->l, NX_BAR_H + 2, NX_ROW_H, 6, rows_n(), 0, nx_ms());
}

static void chains_draw(nx_gc *gc, const void *st)
{
    const struct chains *c = st;
    nx_comp_title_target(gc, nx_str(NX_S_CHAIN_TITLE), NX_S_DETAIL_RUN_ON, c->target);
    nx_list_draw(gc, &c->l, nx_accent(c->target), chains_row, c);
}

static void show_dash(unsigned target)   /* it plays there: that generator's dashboard */
{
    nx_ui_set_focus(target);
    nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
}

static int chains_key(void *st, const struct nx_kev *e)
{
    struct chains *c = st;
    if (nx_list_key(&c->l, e, nx_ms()))
        return 1;
    if (e->type != NXK_PRESS)
        return 1;
    unsigned id = c->l.sel + 1u;
#if NX_FEAT_PRESET
    if (id > LIST_N && id <= LIST_N + presets_n() && (e->keys == NX_KEY_START || e->keys == NX_KEY_LOOP)) {
        unsigned n = id - LIST_N - 1u;
        if (e->keys == NX_KEY_START && nx_ui_act(NXA_LOAD, c->target, (uint16_t)(NX_UC_PRESET + n), 0) == NX_OK
            && nx_ui_act(NXA_PLAY, c->target, 0, (nx_uc_preset_info(n) & NX_UC_PRESET_LOOP) != 0u) == NX_OK)
            show_dash(c->target);
        return 1;
    }
#endif
#if NX_FEAT_TERRAIN
    if (id > LIST_N && (e->keys == NX_KEY_START || e->keys == NX_KEY_LOOP)) {
        unsigned tid = id - LIST_N - presets_n();
        if (e->keys == NX_KEY_START && nx_uc_terrain_play(c->target, tid) == NX_OK)
            nx_scr_terrain_started(c->target, tid);
        else
            nx_ui_push(&nx_scr_terrain, tid | (uint32_t)c->target << 8, NX_TR_PUSH);
        return 1;
    }
#endif
#if NX_FEAT_PLANS
    if (id > NX_CHAINS && (e->keys == NX_KEY_START || e->keys == NX_KEY_LOOP)) {
        unsigned pid = id - NX_CHAINS;
        if (e->keys == NX_KEY_START && nx_uc_plan_play(c->target, pid) == NX_OK) {
            char n[4], d[4];
            nx_fmt_u(n, pid);
            nx_fmt_u(d, nx_uc_plans()[pid - 1u].day + 1u);
            show_dash(c->target);
            nx_ui_toast_s(NX_I20_CHAIN, nx_accent(c->target), NX_S_PLAN_STARTED, n, d);
        } else {
            nx_ui_push(&nx_scr_plan_edit, PLAN_ARG | pid, NX_TR_PUSH);   /* LOOP, or nothing to play */
        }
        return 1;
    }
#endif
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        c->target = e->keys == NX_KEY_G2;
        nx_inval(k_title);
        nx_inval(nx_list_view(&c->l));
        break;
    case NX_KEY_START:
        if (!nx_uc_chains()[id - 1u].n) {
            nx_ui_push(&nx_scr_chain_edit, id, NX_TR_PUSH);
        } else if (nx_uc_chain_play(c->target, id) == NX_OK) {
            char n[4];
            nx_fmt_u(n, id);
            show_dash(c->target);
            nx_ui_toast_s(NX_I20_CHAIN, nx_accent(c->target), NX_S_CHAIN_STARTED, n,
                          nx_str(c->target ? NX_S_GEN_G2 : NX_S_GEN_G1));
        }
        break;
    case NX_KEY_LOOP:
        nx_ui_push(&nx_scr_chain_edit, id, NX_TR_PUSH);
        break;
    case NX_KEY_STOP:
        nx_ui_pop(NX_TR_BACK);
        break;
    default:
        break;
    }
    return 1;
}

static void chains_tick(void *st, uint32_t now)
{
    struct chains *c = st;
    nx_list_tick(&c->l, now);
    if (g_vm.g[0].chain != g_vm_seen.g[0].chain || g_vm.g[1].chain != g_vm_seen.g[1].chain
        || g_vm.g[0].slot != g_vm_seen.g[0].slot || g_vm.g[1].slot != g_vm_seen.g[1].slot
        || g_vm.g[0].plan_day != g_vm_seen.g[0].plan_day || g_vm.g[1].plan_day != g_vm_seen.g[1].plan_day
        || g_vm.g[0].terrain != g_vm_seen.g[0].terrain || g_vm.g[1].terrain != g_vm_seen.g[1].terrain
        || g_vm.g[0].t_day != g_vm_seen.g[0].t_day || g_vm.g[1].t_day != g_vm_seen.g[1].t_day)
        nx_inval(nx_list_view(&c->l));
}

const struct nx_scr nx_scr_chains = {
    chains_enter, chains_key, chains_tick, chains_draw, NX_S_HINT_CHAINS, 0, NX_KEY_UP | NX_KEY_DOWN, 0,
    sizeof(struct chains),
};

/* ---- editor: a chain's programs, or a plan's days (the chain of each) ---- */

struct cedit {
    struct nx_list l;
    uint8_t id, changed, plan;
};

struct items {
    uint8_t *v, *n;        /* the values (slots / chains) and their count */
    uint8_t max, range;    /* at most max values of 1..range */
};

static struct items items_of(const struct cedit *c)
{
#if NX_FEAT_PLANS
    if (c->plan) {
        struct nx_plan *p = &nx_uc_plans()[c->id - 1u];
        return (struct items){ p->chain, &p->n, NX_PLAN_DAYS, NX_CHAINS };
    }
#endif
    struct nx_chain *ch = &nx_uc_chains()[c->id - 1u];
    return (struct items){ ch->slot, &ch->n, NX_CHAIN_ITEMS, 30 };
}

static uint8_t edit_rows(const struct items *it)   /* the values, then "Add" while there is room */
{
    return (uint8_t)(*it->n + (*it->n < it->max ? 1u : 0u));
}

static void edit_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel_ms;
    const struct cedit *c = ctx;
    struct items it = items_of(c);
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2);
    if (i >= *it.n) {
        nx_icon(gc, &nx_icons20, NX_I20_ADD, NX_M + 4, ty, nx_col(gc, NX_PAL_TEXT2));
        nx_text(gc, &nx_font_body16, NX_M + 34, ty, nx_str(c->plan ? NX_S_PLAN_ADD : NX_S_CHAIN_ADD),
                nx_col(gc, NX_PAL_TEXT2));
        return;
    }
    char buf[NX_UC_NAME_MAX + 1u];
    int16_t x = NX_M + 62;
#if NX_FEAT_PLANS
    if (c->plan) {   /* "Day 3   Chain 2   5 programs", the current day marked */
        char a[4];
        const char *args[1] = { a };
        nx_fmt_u(a, i + 1u);
        const struct nx_plan *p = &nx_uc_plans()[c->id - 1u];
        nx_str_format(buf, sizeof buf, NX_S_PLAN_DAY, args, 1);
        nx_text(gc, &nx_font_body16, NX_M + 4, ty, buf, nx_col(gc, i == p->day ? NX_PAL_FOCUS : NX_PAL_TEXT3));
        nx_fmt_u(a, it.v[i]);
        nx_str_format(buf, sizeof buf, NX_S_CHAIN_NAME, args, 1);
        x = nx_text(gc, &nx_font_body16, NX_M + 74, ty, buf, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_u(a, nx_uc_chains()[it.v[i] - 1u].n);
        nx_str_format(buf, sizeof buf, NX_S_CHAIN_PROGRAMS, args, 1);
        nx_text(gc, &nx_font_cap13, (int16_t)(x + 12), (int16_t)(ty + 3), buf, nx_col(gc, NX_PAL_TEXT2));
    } else
#endif
    {
        char *p = nx_fmt_u(buf, i + 1u);
        *p++ = '.';
        *p = 0;
        nx_text_r(gc, &nx_font_body16, NX_M + 24, ty, buf, nx_col(gc, NX_PAL_TEXT3));
        nx_fmt_2d(buf, it.v[i]);
        nx_text(gc, &nx_font_body16, NX_M + 32, ty, buf, nx_col(gc, NX_PAL_TEXT2));
        if (!nx_uc_name(it.v[i], buf))
            buf[0] = 0;
        nx_text_fit(gc, &nx_font_body16, x, ty, sel ? 208 : 234, buf, nx_col(gc, NX_PAL_TEXT1));
    }
    if (sel) {   /* G1 / G2 change it */
        static const char k_lr[] = { (char)NX_CH_lsaquo, ' ', ' ', (char)NX_CH_rsaquo, 0 };
        nx_text_r(gc, &nx_font_body16, NX_LCD_W - NX_M, ty, k_lr, nx_col(gc, NX_PAL_FOCUS));
    }
}

static void edit_enter(void *st, uint32_t arg)
{
    struct cedit *c = st;
    c->plan = (uint8_t)((arg >> 8) & 1u);
    uint8_t lim = c->plan ? (uint8_t)NX_PLANS : (uint8_t)NX_CHAINS;
    c->id = (uint8_t)((arg & 0xFFu) >= 1u && (arg & 0xFFu) <= lim ? (arg & 0xFFu) : 1u);
    struct items it = items_of(c);
    nx_list_init(&c->l, NX_BAR_H + 2, NX_ROW_H, 6, edit_rows(&it), 0, nx_ms());
}

static void edit_draw(nx_gc *gc, const void *st)
{
    const struct cedit *c = st;
    char a[4], rep[16], t[48];
    const char *args[2] = { a, rep };
    nx_fmt_u(a, c->id);
#if NX_FEAT_PLANS
    if (c->plan) {   /* "Plan 1 · day 3/11" */
        const struct nx_plan *p = &nx_uc_plans()[c->id - 1u];
        char *q = nx_fmt_u(rep, p->day < p->n ? p->day + 1u : p->n);
        *q++ = '/';
        nx_fmt_u(q, p->n);
        nx_str_format(t, sizeof t, NX_S_PLAN_EDIT, args, 2);
    } else
#endif
    {
        repeat_text(rep, nx_uc_chains()[c->id - 1u].repeat);
        nx_str_format(t, sizeof t, NX_S_CHAIN_EDIT, args, 2);
    }
    nx_comp_title(gc, t, NX_LCD_W - NX_M, 1);
    nx_list_draw(gc, &c->l, NX_PAL_FOCUS, edit_row, c);
}

static void edit_changed(struct cedit *c, uint8_t sel)
{
    struct items it = items_of(c);
    c->changed = 1;
    uint32_t now = nx_ms();
    nx_list_init(&c->l, c->l.y0, c->l.row_h, c->l.rows, edit_rows(&it), 0, now);
    nx_list_select(&c->l, sel, now);
    nx_inval(k_title);
    nx_inval(nx_list_view(&c->l));
}

static int edit_key(void *st, const struct nx_kev *e)
{
    struct cedit *c = st;
    struct items it = items_of(c);
    if (nx_list_key(&c->l, e, nx_ms()))
        return 1;
    unsigned sel = c->l.sel, n = *it.n;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        if ((e->type == NXK_PRESS || e->type == NXK_REPEAT) && sel < n) {
            unsigned v = it.v[sel] - 1u;
            v = e->keys == NX_KEY_G2 ? (v + 1u) % it.range : (v + it.range - 1u) % it.range;
            it.v[sel] = (uint8_t)(v + 1u);
            c->changed = 1;
            nx_inval(nx_list_row(&c->l, sel));
        }
        return 1;
    case NX_KEY_START:
        switch (nx_keys_act(e, 1)) {
        case NXKA_SHORT:   /* a copy of the selected one after it (the first: 1) */
            if (n < it.max) {
                unsigned at = sel < n ? sel + 1u : n;
                uint8_t v = n ? it.v[sel < n ? sel : n - 1u] : 1u;
                if (!n && !c->plan)
                    nx_uc_chains()[c->id - 1u].repeat = 1;
                memmove(&it.v[at + 1u], &it.v[at], n - at);
                it.v[at] = v;
                (*it.n)++;
                edit_changed(c, (uint8_t)at);
            }
            break;
        case NXKA_HOLD:
            if (sel < n) {
                memmove(&it.v[sel], &it.v[sel + 1u], n - sel - 1u);
                (*it.n)--;
#if NX_FEAT_PLANS
                if (c->plan) {   /* the current day stays the same day */
                    struct nx_plan *p = &nx_uc_plans()[c->id - 1u];
                    if (sel < p->day)
                        p->day--;
                    if (p->day > p->n)
                        p->day = p->n;
                }
#endif
                edit_changed(c, (uint8_t)(sel < *it.n ? sel : *it.n ? *it.n - 1u : 0u));
            }
            break;
        default:
            break;
        }
        return 1;
    case NX_KEY_LOOP:
        if (e->type != NXK_PRESS)
            return 1;
#if NX_FEAT_PLANS
        if (c->plan) {   /* back to day 1 */
            char a[4];
            nx_fmt_u(a, c->id);
            nx_uc_plans()[c->id - 1u].day = 0;
            nx_ui_toast_s(NX_I20_CHAIN, NX_PAL_TEXT2, NX_S_PLAN_RESTARTED, a, 0);
        } else
#endif
        {
            static const uint8_t k_rep[6] = { 1, 2, 3, 5, 10, 0 };
            struct nx_chain *ch = &nx_uc_chains()[c->id - 1u];
            unsigned i = 0;
            while (i < 5u && k_rep[i] != ch->repeat)
                i++;
            ch->repeat = k_rep[(i + 1u) % 6u];
        }
        c->changed = 1;
        nx_inval(k_title);
        nx_inval(nx_list_view(&c->l));
        return 1;
    case NX_KEY_STOP:
        if (e->type == NXK_PRESS) {
            if (c->changed)
                nx_uc_chains_save();
            nx_ui_pop(NX_TR_BACK);
        }
        return 1;
    default:
        return 1;
    }
}

static void edit_tick(void *st, uint32_t now)
{
    nx_list_tick(&((struct cedit *)st)->l, now);
}

const struct nx_scr nx_scr_chain_edit = {
    edit_enter, edit_key, edit_tick, edit_draw, NX_S_HINT_CHAIN_EDIT, 0,
    NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2, 0, sizeof(struct cedit),
};

#if NX_FEAT_PLANS
const struct nx_scr nx_scr_plan_edit = {   /* the same editor, its argument PLAN_ARG | plan */
    edit_enter, edit_key, edit_tick, edit_draw, NX_S_HINT_PLAN_EDIT, 0,
    NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2, 0, sizeof(struct cedit),
};
#endif
#endif
