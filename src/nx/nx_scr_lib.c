/* Program list (docs/nx/design/ui.md W7) and program detail (W9). The list works and looks like
 * the original's "Program G1/G2" list, restyled: 30 slots as "NN. name", the selected row a bar in
 * the target generator's colour, the target in the title; an empty slot loads like any other (as
 * the empty program). The detail reads its slot's header and frequencies from the SPI flash
 * through nx_uicore.h (storage_slot_read), never from g_ch.
 *
 * List keys, as the original's: UP/DOWN select, G1/G2 the generator it loads into, START loads
 * the slot and shows that generator's dashboard, STOP shows it without loading; nx adds LOOP held
 * = the program detail.
 * The detail opens from the library (LOOP held on a slot) and from the dashboard (LOOP held: the
 * focused generator's program). Detail keys: UP/DOWN scroll the frequencies, G1/G2 target, START
 * loads and plays the slot on the target and returns to its dashboard (on the target's own
 * loaded program START plays or resumes it without a reload, which would restart it from step 1),
 * STOP back. */
#include <string.h>

#include "nx_comp.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_slots.h"
#include "nx_uifmt.h"

#define SLOTS 30u
#define PAGE 16u
#define NAME_X0 44
#define NAME_X1 (NX_LCD_W - NX_M)
/* Rows of 30 px: the program list runs down to the bottom of the screen (7 slots, as the
 * original's list), the detail's steps stop above its key-hint bar (the settings pages keep
 * NX_ROW_H). */
#define LIST_ROW_H 30
#define LIB_ROWS 7
#define DETAIL_ROWS 5   /* 52 + 5 x 30: above the key-hint bar */
#define LIST_TEXT_DY ((LIST_ROW_H - 20) / 2)   /* a body16 line or a 20 px icon, centred */

static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };

static void slot_name(unsigned slot, char *out)
{
    if (!nx_uc_name(slot, out)) {
        char n[4];
        nx_fmt_2d(n, slot);
        const char *args[1] = { n };
        nx_str_format(out, NX_UC_NAME_MAX + 1u, NX_S_LIB_SLOT_N, args, 1);
    }
}

/* ---- library ---- */

struct lib {
    struct nx_list l;
    uint8_t target;
    uint8_t pick;   /* NX_SLOTOP_COPY / _MOVE: choose the target slot for src (NX_FEAT_SLOTS) */
    int16_t mq_last;
    uint8_t src;
};

#if NX_FEAT_SLOTS
static void slot_refused(int rc)
{
    nx_ui_toast_s(NX_I20_WARNING, NX_PAL_WARNING,
                  rc == NX_ENOSPACE ? NX_S_SLOT_NOT_EMPTY : rc == NX_BUSY ? NX_S_SLOT_IN_USE
                  : rc == NX_EPENDING ? NX_S_SLOT_PENDING : rc == NX_EINVAL ? NX_S_SLOT_EMPTY : NX_S_SLOT_FAILED, 0, 0);
}
#endif

static void lib_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    char buf[NX_UC_NAME_MAX + 1u];
    int16_t ty = (int16_t)(r.y0 + LIST_TEXT_DY);
    char *p = nx_fmt_2d(buf, i + 1u);
    p[0] = '.';
    p[1] = 0;
    nx_text(gc, &nx_font_body16, 14, ty, buf, nx_col(gc, sel ? NX_PAL_ON_ACCENT : NX_PAL_TEXT3));
    slot_name(i + 1u, buf);
    nx_text_box(gc, &nx_font_body16, (nx_rect){ NAME_X0, ty, NAME_X1, (int16_t)(ty + 20) }, buf,
                nx_col(gc, sel ? NX_PAL_ON_ACCENT : NX_PAL_TEXT1), sel, sel_ms);
}

/* The selection stays where it was when the library opens again, for either generator, as the
 * original's program list keeps it (the intent scenarios rely on it). */
static uint8_t s_lib_sel, s_lib_top;   /* and its window, as the original's list_top */

void nx_scr_lib_boot(void)
{
    s_lib_sel = s_lib_top = 0;
}

static void lib_enter(void *st, uint32_t arg)
{
    struct lib *b = st;
    b->target = (uint8_t)(arg & 1u);
    b->src = (uint8_t)(arg >> 8);
    b->pick = (uint8_t)((arg >> 16) & 3u);
    nx_list_init(&b->l, NX_BAR_H + 5, LIST_ROW_H, LIB_ROWS, SLOTS, 0, nx_ms());   /* box 25..239 */
    b->l.filled = 1;
    unsigned sel = s_lib_sel;
#if NX_FEAT_SLOTS
    if (b->pick) {   /* the first empty slot after the source */
        sel = b->src % SLOTS;
        for (unsigned i = 0; i < SLOTS && nx_uc_slot_used(sel + 1u); i++)
            sel = (sel + 1u) % SLOTS;
    } else
#endif
        b->l.top = s_lib_top;   /* the window where it was; nx_list_select keeps the selection in it */
    nx_list_select(&b->l, (uint8_t)sel, nx_ms());
}

static void lib_draw(nx_gc *gc, const void *st)
{
    const struct lib *b = st;
#if NX_FEAT_SLOTS
    if (b->pick) {   /* "Copy 04 to" */
        char n[4], t[32];
        const char *args[1] = { n };
        nx_fmt_2d(n, b->src);
        nx_str_format(t, sizeof t, b->pick == NX_SLOTOP_MOVE ? NX_S_SLOT_PICK_MOVE : NX_S_SLOT_PICK_COPY, args, 1);
        nx_comp_title(gc, t, NX_LCD_W - NX_M, 1);
        nx_list_draw(gc, &b->l, NX_PAL_FOCUS, lib_row, 0);
        return;
    }
#endif
    const char *t = nx_str(NX_S_LIB_TITLE);   /* "Program", then the target: "Program [G1]" */
    nx_comp_title(gc, t, NX_LCD_W - NX_M, 0);   /* no "‹": the original list has none */
    nx_comp_badge(gc, (int16_t)(NX_M + (int16_t)nx_font_text_width(&nx_font_body16, t) + 8), 2, b->target, 1);
    nx_list_draw(gc, &b->l, nx_accent(b->target), lib_row, 0);
}

static int lib_key(void *st, const struct nx_kev *e)
{
    struct lib *b = st;
    if (nx_list_key(&b->l, e, nx_ms())) {
        b->mq_last = 0;
        if (!b->pick) {
            s_lib_sel = b->l.sel;
            s_lib_top = b->l.top;
        }
        return 1;
    }
    unsigned slot = b->l.sel + 1u;
#if NX_FEAT_SLOTS
    if (b->pick) {   /* START: the target; STOP: back to the detail */
        if (e->type != NXK_PRESS)
            return 1;
        if (e->keys == NX_KEY_STOP) {
            nx_ui_pop(NX_TR_BACK);
        } else if (e->keys == NX_KEY_START && slot != b->src) {
            int rc = nx_uc_slot_op(b->pick, b->src, slot);
            if (rc != NX_OK) {
                slot_refused(rc);
            } else {
                uint8_t move = b->pick == NX_SLOTOP_MOVE;
                nx_ui_pop(NX_TR_BACK);
                if (move)
                    nx_ui_pop(NX_TR_NONE);   /* the detail of the slot that is emptied */
            }
        }
        return 1;
    }
#endif
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        if (e->type == NXK_PRESS) {
            b->target = e->keys == NX_KEY_G2;
            nx_inval(k_title);
            nx_inval(nx_list_view(&b->l));
        }
        return 1;
    case NX_KEY_START:
        if (e->type == NXK_PRESS && nx_ui_act(NXA_LOAD, b->target, (uint16_t)slot, 0) == NX_OK) {
            char n[4];
            nx_fmt_2d(n, slot);
            nx_ui_set_focus(b->target);
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
            nx_ui_toast_s(NX_I20_CHECK_CIRCLE, nx_accent(b->target), NX_S_TOAST_LOADED, n,
                          nx_str(b->target ? NX_S_GEN_G2 : NX_S_GEN_G1));
        }
        return 1;
    case NX_KEY_LOOP:
        if (nx_keys_act(e, 1) == NXKA_HOLD)
            nx_ui_push(&nx_scr_detail, slot | ((uint32_t)b->target << 8), NX_TR_PUSH);
        return 1;
    case NX_KEY_STOP:   /* back without loading, to the target's dashboard (the original's list) */
        if (e->type == NXK_PRESS) {
            nx_ui_set_focus(b->target);
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
        }
        return 1;
    default:
        return 1;
    }
}

static void lib_tick(void *st, uint32_t now)
{
    struct lib *b = st;
    nx_list_tick(&b->l, now);
    char name[NX_UC_NAME_MAX + 1u];
    slot_name(b->l.sel + 1u, name);
    nx_rect row = nx_list_row(&b->l, b->l.sel);
    nx_marquee_tick(&b->mq_last, (int16_t)nx_font_text_width(&nx_font_body16, name), NAME_X1 - NAME_X0,
                    now - b->l.t0, (nx_rect){ NAME_X0, row.y0, NAME_X1, row.y1 });
}

const struct nx_scr nx_scr_lib = {
    lib_enter, lib_key, lib_tick, lib_draw, NX_NONE, 0, NX_KEY_UP | NX_KEY_DOWN, 0, sizeof(struct lib),
};

_Static_assert(sizeof(struct lib) <= NX_UI_ST_MAX, "library state fits a stack level");

/* ---- program detail ---- */

struct detail {
    struct nx_list l;
    uint8_t slot, target;
};

static struct nx_slot_info s_info;
static uint64_t s_page[PAGE];   /* frequencies of steps s_page_base .. s_page_base + PAGE - 1 */
static uint8_t s_page_base, s_page_n;

static void page_for(unsigned slot, unsigned top)
{
    unsigned base = top > 4u ? top - 4u : 0u;
    if (s_page_n && base >= s_page_base && top + DETAIL_ROWS + 1u <= (unsigned)s_page_base + s_page_n)
        return;   /* the rows nx_list_draw draws, top - 1 .. top + DETAIL_ROWS, are in the page */
    s_page_base = (uint8_t)base;
    s_page_n = (uint8_t)nx_uc_slot_freqs(slot, base, s_page, PAGE);
}

static void detail_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel;
    (void)sel_ms;
    const struct detail *d = ctx;
    char buf[40];
    int16_t ty = (int16_t)(r.y0 + LIST_TEXT_DY);
    nx_fmt_u(buf, i + 1u);
    nx_text_r(gc, &nx_font_body16, 48, ty, buf, nx_col(gc, NX_PAL_TEXT3));
    if (i >= s_page_base && i < (unsigned)s_page_base + s_page_n) {
        char *p = nx_fmt_freq(buf, s_page[i - s_page_base], 3, 8, 1);
        *p++ = ' ';
        strcpy(p, nx_str(NX_S_UNIT_HZ));
        nx_text(gc, &nx_font_body16, 60, ty, buf, nx_col(gc, NX_PAL_TEXT1));
    }
    for (unsigned ch = 0; ch < 2u; ch++) {   /* where a generator plays this slot */
        const struct nx_gen_vm *g = &g_vm_draw.g[ch];
        if (g->slot == d->slot && g->step == i + 1u && (g->state == NXG_RUNNING || g->state == NXG_PAUSED)) {
            int16_t x = (int16_t)(308 - 56 + (int16_t)ch * 28);
            nx_icon(gc, &nx_icons20, g->state == NXG_RUNNING ? NX_I20_PLAY : NX_I20_PAUSE, x, ty,
                    nx_col(gc, nx_accent(ch)));
        }
    }
}

/* 2 plays the slot (running or paused), 1 only has it loaded, 0 not this slot */
static int play_rank(const struct nx_gen_vm *g, unsigned slot)
{
    if (g->slot != slot || !g->step)
        return 0;
    return g->state == NXG_RUNNING || g->state == NXG_PAUSED ? 2 : 1;
}

static void detail_enter(void *st, uint32_t arg)
{
    struct detail *d = st;
    d->slot = (uint8_t)(arg & 0xFFu);
    d->target = (uint8_t)((arg >> 8) & 1u);
    nx_uc_slot_info(d->slot, &s_info);
    s_page_n = 0;
    unsigned n = s_info.used ? s_info.count : 0u;
    /* open at the step of the generator that plays this slot (running or paused) rather than one
     * that only has it loaded; the target first on a tie */
    const struct nx_gen_vm *g = &g_vm.g[d->target], *o = &g_vm.g[d->target ^ 1u];
    if (play_rank(o, d->slot) > play_rank(g, d->slot))
        g = o;
    unsigned sel = play_rank(g, d->slot) ? g->step - 1u : 0u;
    nx_list_init(&d->l, 52, LIST_ROW_H, DETAIL_ROWS, (uint8_t)(n > 200u ? 200u : n), (uint8_t)sel, nx_ms());
    page_for(d->slot, d->l.top);
}

static void detail_draw(nx_gc *gc, const void *st)
{
    const struct detail *d = st;
    char name[NX_UC_NAME_MAX + 8u], line[NX_FMT_SUMMARY_MAX];
    nx_fmt_2d(name, d->slot);
    size_t n = strlen(name);
    name[n++] = ' ';
    name[n++] = (char)NX_CH_middot;
    name[n++] = ' ';
    slot_name(d->slot, name + n);
    nx_comp_title_target(gc, name, NX_S_DETAIL_RUN_ON, d->target);
    if (s_info.used) {   /* whole fields only: drop the last " · field" until the line fits */
        nx_fmt_summary(line, &s_info);
        char *cut;
        while (nx_font_text_width(&nx_font_cap13, line) > 296u && (cut = strrchr(line, (char)NX_CH_middot)) && cut > line)
            cut[-1] = 0;
        nx_text_fit(gc, &nx_font_cap13, NX_M, 28, 296, line, nx_col(gc, NX_PAL_TEXT2));
    }
    nx_list_draw(gc, &d->l, nx_accent(d->target), detail_row, d);
}

static int detail_key(void *st, const struct nx_kev *e)
{
    struct detail *d = st;
    if (nx_list_key(&d->l, e, nx_ms())) {
        page_for(d->slot, d->l.top);
        return 1;
    }
    if (e->type != NXK_PRESS)
        return 1;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        d->target = e->keys == NX_KEY_G2;
        nx_inval(k_title);
        nx_inval(nx_list_view(&d->l));
        return 1;
    case NX_KEY_START: {
        const struct nx_gen_vm *g = &g_vm.g[d->target];
        int ok;
        if (g->state != NXG_NONE && g->slot == d->slot)   /* its own program: no reload (step 1) */
            ok = g->state == NXG_RUNNING
                 || nx_ui_act(NXA_PLAY, d->target, 0, g->state == NXG_FINISHED ? 0u : g->loop) == NX_OK;
        else
            ok = nx_ui_act(NXA_LOAD, d->target, d->slot, 0) == NX_OK && nx_ui_act(NXA_PLAY, d->target, 0, 0) == NX_OK;
        if (ok) {
            nx_ui_set_focus(d->target);
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
        }
        return 1;
    }
    case NX_KEY_STOP:
        nx_ui_pop(NX_TR_BACK);
        return 1;
#if NX_FEAT_SLOTS
    case NX_KEY_LOOP:   /* play on both, copy, move, erase */
        nx_ui_push(&nx_scr_slotact, d->slot, NX_TR_SHEET);
        return 1;
#endif
    default:
        return 1;
    }
}

static void detail_tick(void *st, uint32_t now)
{
    struct detail *d = st;
    nx_list_tick(&d->l, now);
    for (unsigned ch = 0; ch < 2u; ch++)
        if (g_vm.g[ch].step != g_vm_seen.g[ch].step || g_vm.g[ch].state != g_vm_seen.g[ch].state)
            nx_inval(nx_list_view(&d->l));   /* the playing marker moved */
}

const struct nx_scr nx_scr_detail = {
    detail_enter, detail_key, detail_tick, detail_draw, NX_S_HINT_DETAIL, 0, NX_KEY_UP | NX_KEY_DOWN, 0,
    sizeof(struct detail),
};

#if NX_FEAT_SLOTS
/* ---- slot actions (program detail > LOOP): a sheet; UP/DOWN select, START does it, STOP
 * closes. Copy / move ask for the target slot in the program list, erase for a hold-START
 * confirmation. ---- */

#define ACT_H 192   /* title, four rows, above the key-hint bar */
#define ACT_Y0 (NX_LCD_H - ACT_H)
#define ACT_ROW_H 32

enum { A_BOTH, A_COPY, A_MOVE, A_ERASE, A_N };

struct slotact {
    struct nx_list l;
    uint8_t slot;
};

static uint8_t s_act_slot;   /* the slot the erase confirmation is about */

static void act_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    (void)sel_ms;
    static const uint8_t k_icon[A_N] = { NX_I20_PLAY, NX_I20_COPY, NX_I20_MOVE, NX_I20_DELETE };
    static const uint16_t k_label[A_N] = { NX_S_SLOT_PLAY_BOTH, NX_S_SLOT_COPY, NX_S_SLOT_MOVE, NX_S_SLOT_ERASE };
    int16_t ty = (int16_t)(r.y0 + (ACT_ROW_H - 20) / 2);
    uint16_t c = nx_col(gc, sel ? NX_PAL_ON_ACCENT : i == A_ERASE ? NX_PAL_WARNING : NX_PAL_TEXT1);
    nx_icon(gc, &nx_icons20, k_icon[i], NX_M + 4, ty, c);
    nx_text(gc, &nx_font_body16, NX_M + 34, ty, nx_str(k_label[i]), c);
}

static void act_enter(void *st, uint32_t arg)
{
    struct slotact *a = st;
    a->slot = (uint8_t)arg;
    nx_list_init(&a->l, ACT_Y0 + 38, ACT_ROW_H, A_N, A_N, 0, nx_ms());
    a->l.filled = 1;
}

static void act_draw(nx_gc *gc, const void *st)
{
    const struct slotact *a = st;
    char n[4], t[32];
    const char *args[1] = { n };
    nx_fmt_2d(n, a->slot);
    nx_str_format(t, sizeof t, NX_S_SLOT_TITLE, args, 1);
    nx_sheet_frame(gc, ACT_Y0);
    nx_text_c(gc, &nx_font_body16, NX_LCD_W / 2, ACT_Y0 + 12, t, nx_col(gc, NX_PAL_TEXT1));
    nx_list_draw(gc, &a->l, NX_PAL_FOCUS, act_row, a);
}

static void do_erase(void)   /* the confirmation's yes */
{
    int rc = nx_uc_slot_op(NX_SLOTOP_ERASE, s_act_slot, 0);
    if (rc != NX_OK)
        slot_refused(rc);
    else
        nx_ui_pop(NX_TR_BACK);   /* the detail of the slot that is erased */
}

static int act_key(void *st, const struct nx_kev *e)
{
    struct slotact *a = st;
    if (nx_list_key(&a->l, e, nx_ms()))
        return 1;
    if (e->type != NXK_PRESS)
        return 1;
    if (e->keys == NX_KEY_STOP) {
        nx_ui_pop(NX_TR_NONE);
        return 1;
    }
    if (e->keys != NX_KEY_START)
        return 1;
    unsigned slot = a->slot;
    switch (a->l.sel) {
    case A_BOTH:
        if (nx_ui_act(NXA_LOAD, 0, (uint16_t)slot, 0) == NX_OK && nx_ui_act(NXA_PLAY, 0, 0, 0) == NX_OK
            && nx_ui_act(NXA_LOAD, 1, (uint16_t)slot, 0) == NX_OK && nx_ui_act(NXA_PLAY, 1, 0, 0) == NX_OK)
            nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
        break;
    case A_COPY:
    case A_MOVE:
        if (!nx_uc_slot_used(slot)) {
            slot_refused(NX_EINVAL);
            break;
        }
        nx_ui_pop(NX_TR_NONE);
        nx_ui_push(&nx_scr_lib, (uint32_t)(a->l.sel == A_MOVE ? NX_SLOTOP_MOVE : NX_SLOTOP_COPY) << 16 | slot << 8,
                   NX_TR_PUSH);
        break;
    default:
        s_act_slot = (uint8_t)slot;
        nx_ui_pop(NX_TR_NONE);
        nx_ui_confirm(NX_S_SLOT_ERASE_Q, NX_S_SLOT_ERASE_BODY, do_erase);
        break;
    }
    return 1;
}

static void act_tick(void *st, uint32_t now)
{
    nx_list_tick(&((struct slotact *)st)->l, now);
}

const struct nx_scr nx_scr_slotact = {
    act_enter, act_key, act_tick, act_draw, NX_S_HINT_SLOT, NX_SF_SHEET, NX_KEY_UP | NX_KEY_DOWN, ACT_H,
    sizeof(struct slotact),
};
#endif
