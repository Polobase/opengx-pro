/* The biofeedback scan (menu > Biofeedback; NX_FEAT_BFB, nx_bfb.h): its settings as a list, the
 * scan's progress below them, and the hits once there are some.
 *
 * Keys: UP/DOWN select, G1/G2 change the value (held: repeat), START starts the scan on the
 * chosen generator, STOP ends a running scan (the generator stops), else back; LOOP shows the
 * hits of the last scan instead of the settings, and back. */
#include "nx_config.h"

#if NX_FEAT_BFB
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

enum { R_GEN, R_AMPL, R_DWELL, R_SLOT, ROWS };
static const uint16_t k_label[ROWS] = { NX_S_DETAIL_RUN_ON, NX_S_MANUAL_AMPL, NX_S_BFB_DWELL, NX_S_BFB_SLOT };

#define LIST_Y0 (NX_BAR_H + 2)
#define STATUS_Y0 (LIST_Y0 + ROWS * NX_ROW_H)   /* 152: the status lines below the list */
static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };
static const nx_rect k_status = { 0, STATUS_Y0, NX_LCD_W, NX_HINT_Y };

struct bfbs {
    struct nx_list l;
    uint8_t hits;           /* the list shows the hits, not the settings */
    uint8_t state, nhits;   /* what the status was drawn from */
    uint16_t step;
};

static int scanning(void)
{
    uint8_t st = nx_uc_bfb()->state;
    return st != NX_BFB_IDLE && st < NX_BFB_DONE;
}

static void init_list(struct bfbs *s)
{
    uint8_t n = s->hits ? nx_uc_bfb()->nhits : (uint8_t)ROWS;
    nx_list_init(&s->l, LIST_Y0, NX_ROW_H, s->hits ? 6u : (uint8_t)ROWS, n, 0, nx_ms());
}

static void row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel_ms;
    const struct bfbs *s = ctx;
    const struct nx_bfb_cfg *c = nx_uc_bfb_cfg();
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2), rx = NX_LCD_W - NX_M - (sel && !s->hits ? 16 : 0);
    char v[32];
    const char *t = v;
    uint16_t col = nx_col(gc, NX_PAL_TEXT1);
    if (s->hits) {   /* "41 010.25 Hz" and the deviation */
        const struct nx_bfb_hit *h = &nx_uc_bfb()->hits[i];
        char *p = nx_fmt_freq(v, h->code, 2, 2, 0);
        strcpy(p, " Hz");
        nx_text(gc, &nx_font_body16, NX_M + 4, ty, v, col);
        p = v;
        *p++ = '+';
        nx_fmt_u(p, h->dev20 / 20u);
        nx_text_r(gc, &nx_font_body16, rx, ty, v, nx_col(gc, NX_PAL_TEXT2));
        return;
    }
    nx_text(gc, &nx_font_body16, NX_M + 4, ty, nx_str(k_label[i]), nx_col(gc, NX_PAL_TEXT2));
    switch (i) {
    case R_GEN:
        t = nx_str(c->ch ? NX_S_GEN_G2 : NX_S_GEN_G1);
        col = nx_col(gc, nx_accent(c->ch));
        break;
    case R_AMPL:
        nx_fmt_cv(v, c->ampl_cv);
        break;
    case R_DWELL:
        nx_fmt_dur(v, c->dwell_s);
        break;
    default:
        nx_fmt_u(v, c->slot);
        break;
    }
    nx_text_r(gc, &nx_font_body16, rx, ty, t, col);
    if (sel) {   /* G1 / G2 change it */
        static const char k_lr[] = { (char)NX_CH_lsaquo, 0 };
        static const char k_rr[] = { (char)NX_CH_rsaquo, 0 };
        nx_text_r(gc, &nx_font_body16, NX_LCD_W - NX_M, ty, k_rr, nx_col(gc, NX_PAL_FOCUS));
        nx_text(gc, &nx_font_body16, 150, ty, k_lr, nx_col(gc, NX_PAL_FOCUS));
    }
}

static void status(nx_gc *gc)
{
    const struct nx_bfb_view *v = nx_uc_bfb();
    char a[3][12], buf[64], f[24];
    const char *args[3] = { a[0], a[1], a[2] };
    uint16_t sid = NX_S_BFB_IDLE;
    uint8_t tok = NX_PAL_TEXT2;
    nx_fmt_u(a[0], v->step);
    nx_fmt_u(a[1], NX_BFB_STEPS);
    nx_fmt_u(a[2], v->nhits);
    switch (v->state) {
    case NX_BFB_AUTH:
    case NX_BFB_LOAD:
        sid = NX_S_BFB_AUTH;
        break;
    case NX_BFB_RAMP:
        sid = NX_S_BFB_RAMP;
        break;
    case NX_BFB_BASE:
        sid = NX_S_BFB_BASE;
        break;
    case NX_BFB_SWEEP:
    case NX_BFB_SAVE:
        sid = NX_S_BFB_SCAN;
        break;
    case NX_BFB_DONE:
        nx_fmt_u(a[0], v->nhits);
        nx_fmt_u(a[1], v->slot);
        sid = v->nhits ? NX_S_BFB_SAVED : NX_S_BFB_NONE;
        tok = v->nhits ? NX_PAL_SUCCESS : NX_PAL_WARNING;
        break;
    case NX_BFB_FAILED:
        sid = NX_S_BFB_FAILED;
        tok = NX_PAL_ERROR;
        break;
    default:
        break;
    }
    nx_str_format(buf, sizeof buf, sid, args, 3);
    nx_text(gc, &nx_font_cap13, NX_M + 4, STATUS_Y0 + 4, buf, nx_col(gc, tok));
    if (v->state == NX_BFB_SWEEP || v->state == NX_BFB_SAVE) {   /* the frequency, the reading, the progress */
        char *p = nx_fmt_freq(f, v->code, 2, 2, 0);
        strcpy(p, " Hz");
        nx_text(gc, &nx_font_body16, NX_M + 4, STATUS_Y0 + 22, f, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_u(f, v->reading);
        nx_text_r(gc, &nx_font_body16, NX_LCD_W - NX_M, STATUS_Y0 + 22, f, nx_col(gc, NX_PAL_TEXT2));
        int16_t x1 = (int16_t)(NX_M + (NX_LCD_W - 2 * NX_M) * (uint32_t)v->step / NX_BFB_STEPS);
        nx_fill(gc, (nx_rect){ NX_M, STATUS_Y0 + 46, NX_LCD_W - NX_M, STATUS_Y0 + 50 }, nx_col(gc, NX_PAL_TRACK));
        nx_fill(gc, (nx_rect){ NX_M, STATUS_Y0 + 46, x1, STATUS_Y0 + 50 }, nx_col(gc, nx_accent(v->ch)));
    }
    if (v->missed) {
        nx_fmt_u(a[0], v->missed);
        nx_str_format(buf, sizeof buf, NX_S_BFB_MISSED, args, 1);
        nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, STATUS_Y0 + 4, buf, nx_col(gc, NX_PAL_WARNING));
    }
}

static void bfb_enter(void *st, uint32_t arg)
{
    (void)arg;
    struct bfbs *s = st;
    s->hits = nx_uc_bfb()->nhits != 0u && !scanning();
    init_list(s);
}

static void bfb_draw(nx_gc *gc, const void *st)
{
    const struct bfbs *s = st;
    nx_comp_title_target(gc, nx_str(NX_S_BFB_TITLE), NX_S_DETAIL_RUN_ON, nx_uc_bfb_cfg()->ch);
    nx_list_draw(gc, &s->l, nx_accent(nx_uc_bfb_cfg()->ch), row, s);
    status(gc);
}

static uint32_t step_by(uint32_t v, uint32_t d, int up, uint32_t lo, uint32_t hi)
{
    if (up)
        return v > hi - d ? hi : v + d;
    return v < lo + d ? lo : v - d;
}

static void change(struct bfbs *s, int up)
{
    struct nx_bfb_cfg *c = nx_uc_bfb_cfg();
    switch (s->l.sel) {
    case R_GEN:
        c->ch ^= 1u;
        nx_inval(k_title);
        break;
    case R_AMPL:   /* 0.5 V steps, 1 to 20 V */
        c->ampl_cv = (uint16_t)step_by(c->ampl_cv, 50u, up, 100u, 2000u);
        break;
    case R_DWELL:   /* 30 s steps, 30 s to 60 min */
        c->dwell_s = (uint16_t)step_by(c->dwell_s, 30u, up, 30u, 3600u);
        break;
    default:
        c->slot = (uint8_t)step_by(c->slot, 1u, up, 1u, NX_UC_SLOTS);
        break;
    }
    nx_inval(nx_list_row(&s->l, s->l.sel));
}

static int bfb_key(void *st, const struct nx_kev *e)
{
    struct bfbs *s = st;
    if (nx_list_key(&s->l, e, nx_ms()))
        return 1;
    int press = e->type == NXK_PRESS;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        if ((press || e->type == NXK_REPEAT) && !s->hits && !scanning())
            change(s, e->keys == NX_KEY_G2);
        return 1;
    case NX_KEY_LOOP:
        if (press && nx_uc_bfb()->nhits) {
            s->hits ^= 1u;
            init_list(s);
            nx_inval(nx_list_view(&s->l));
        }
        return 1;
    case NX_KEY_START:
        if (press && !scanning()) {
            int rc = nx_uc_bfb_start();
            if (rc == NX_BUSY)   /* the other generator has the slot, or a scan runs */
                nx_ui_toast_s(NX_I20_WARNING, NX_PAL_WARNING, NX_S_SLOT_IN_USE, 0, 0);
            else if (rc == NX_OK && s->hits) {
                s->hits = 0;
                init_list(s);
                nx_inval(nx_list_view(&s->l));
            }
        }
        return 1;
    case NX_KEY_STOP:
        if (press) {
            if (scanning())
                nx_uc_bfb_stop();
            else
                nx_ui_pop(NX_TR_BACK);
        }
        return 1;
    default:
        return 1;
    }
}

static void bfb_tick(void *st, uint32_t now)
{
    struct bfbs *s = st;
    const struct nx_bfb_view *v = nx_uc_bfb();
    nx_list_tick(&s->l, now);
    if (v->state != s->state || v->step != s->step || v->nhits != s->nhits) {
        s->state = v->state;
        s->step = v->step;
        s->nhits = v->nhits;
        nx_inval(k_status);
        if (s->hits)   /* the hits' rows */
            nx_inval(nx_list_view(&s->l));
    }
}

const struct nx_scr nx_scr_bfb = {
    bfb_enter, bfb_key, bfb_tick, bfb_draw, NX_S_HINT_BFB, NX_SF_AWAKE, NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2,
    0, sizeof(struct bfbs),
};
#endif
