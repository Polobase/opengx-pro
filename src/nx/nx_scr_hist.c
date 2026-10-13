/* History (menu > History, NX_FEAT_HIST): every run, newest first, two lines each: slot and
 * name, then generator, running time, how it ended, the session and the uptime at its start. The
 * title bar shows the all-time running time and runs.
 *
 * Keys: UP/DOWN scroll, STOP back. */
#include "nx_config.h"

#if NX_FEAT_HIST
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

#define HROW_H 42
#define CACHE_N 6u

struct hist {
    struct nx_list l;
    uint16_t n;
};

/* the rows on the screen, read from the SPI flash once (rows are drawn once per band) */
static struct {
    uint16_t i;
    uint8_t ok;
    struct nx_hist_rec r;
} s_cache[CACHE_N];
static uint8_t s_cache_next;

static const struct nx_hist_rec *rec(unsigned i)
{
    for (unsigned k = 0; k < CACHE_N; k++)
        if (s_cache[k].i == i)
            return s_cache[k].ok ? &s_cache[k].r : 0;
    unsigned k = s_cache_next;
    s_cache_next = (uint8_t)((k + 1u) % CACHE_N);
    s_cache[k].i = (uint16_t)i;
    s_cache[k].ok = (uint8_t)nx_uc_hist_get(i, &s_cache[k].r);
    return s_cache[k].ok ? &s_cache[k].r : 0;
}

static void hist_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)ctx;
    (void)sel;
    (void)sel_ms;
    const struct nx_hist_rec *h = rec(i);
    if (!h)
        return;
    static const uint16_t k_end[6] = { NX_S_HIST_END_STOPPED, NX_S_HIST_END_FINISHED, NX_S_HIST_END_STOPPED,
                                       NX_S_HIST_END_TIMER,   NX_S_HIST_END_CUT,      NX_S_HIST_END_PC };
    char name[NX_HIST_NAME + 8u], *p = name;
    if (h->slot <= NX_UC_SLOTS) {   /* a built-in or manual source has no number (nx_src.h) */
        p = nx_fmt_2d(name, h->slot);
        *p++ = ' ';
        *p++ = ' ';
    }
    memcpy(p, h->name, NX_HIST_NAME);
    p[NX_HIST_NAME] = 0;
    nx_text_fit(gc, &nx_font_body16, NX_M + 4, (int16_t)(r.y0 + 3), NX_LCD_W - 2 * NX_M - 8, name,
                nx_col(gc, NX_PAL_TEXT1));
    char dur[16], ses[8], at[12], line[96];
    nx_fmt_dur(dur, h->run_s);
    nx_fmt_u(ses, h->session);
    char *q = nx_fmt_u(at, h->start_s / 3600u);   /* +h:mm */
    *q++ = ':';
    nx_fmt_2d(q, h->start_s / 60u % 60u);
    const char *args[3] = { nx_str(h->ch ? NX_S_GEN_G2 : NX_S_GEN_G1), dur, nx_str(k_end[h->end % 6u]) };
    size_t n = nx_str_format(line, sizeof line, NX_S_HIST_LINE, args, 3);
    line[n++] = ' ';
    line[n++] = (char)NX_CH_middot;
    line[n++] = ' ';
    args[0] = ses;
    args[1] = at;
    nx_str_format(line + n, sizeof line - n, NX_S_HIST_SESSION, args, 2);
    nx_text_fit(gc, &nx_font_cap13, NX_M + 4, (int16_t)(r.y0 + 23), NX_LCD_W - 2 * NX_M - 8, line,
                nx_col(gc, h->end == NX_HEND_FINISHED ? NX_PAL_SUCCESS : NX_PAL_TEXT2));
}

static void hist_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct hist *h = st;
    for (unsigned k = 0; k < CACHE_N; k++)
        s_cache[k].i = 0xFFFFu;
    h->n = (uint16_t)nx_uc_hist_count();
    nx_list_init(&h->l, NX_BAR_H + 2, HROW_H, 4, (uint8_t)(h->n > 255u ? 255u : h->n), 0, nx_ms());
}

static void hist_draw(nx_gc *gc, const void *st)
{
    const struct hist *h = st;
    struct nx_totals t;
    nx_uc_hist_totals(&t);
    char a[16], b[12], line[48];
    const char *args[2] = { a, b };
    const char *m[1] = { b };
    nx_fmt_u(b, t.run_s >= 3600u ? t.run_s / 3600u : t.run_s / 60u);
    nx_str_format(a, sizeof a, t.run_s >= 3600u ? NX_S_HIST_HOURS : NX_S_SET_MINUTES, m, 1);
    nx_fmt_u(b, t.runs);
    nx_str_format(line, sizeof line, NX_S_HIST_TOTAL, args, 2);
    nx_comp_title(gc, nx_str(NX_S_HIST_TITLE), 140, 1);
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, 4, line, nx_col(gc, NX_PAL_TEXT2));
    if (!h->n) {
        nx_text_c(gc, &nx_font_body16, NX_LCD_W / 2, 110, nx_str(NX_S_HIST_EMPTY), nx_col(gc, NX_PAL_TEXT3));
        return;
    }
    nx_list_draw(gc, &h->l, NX_PAL_FOCUS, hist_row, h);
}

static int hist_key(void *st, const struct nx_kev *e)
{
    struct hist *h = st;
    if (nx_list_key(&h->l, e, nx_ms()))
        return 1;
    if (e->type == NXK_PRESS && e->keys == NX_KEY_STOP)
        nx_ui_pop(NX_TR_BACK);
    return 1;
}

static void hist_tick(void *st, uint32_t now)
{
    nx_list_tick(&((struct hist *)st)->l, now);
}

const struct nx_scr nx_scr_hist = {
    hist_enter, hist_key, hist_tick, hist_draw, NX_S_HINT_HIST, 0, NX_KEY_UP | NX_KEY_DOWN, 0, sizeof(struct hist),
};
#endif
