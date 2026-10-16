/* The biofeedback scan (menu > Bio scan, or START held on the dashboard; NX_FEAT_BFB, nx_bfb.h): three pages above the status
 * lines: the settings as a list, the scan graph (the last readings' deviation scrolling by, and
 * the whole sweep's below it with the hits marked; a hit's peak still in the last readings is
 * marked there too), the hits as a list. The sweep brings the
 * graph up; its end goes to the dashboard when there are hits (they play), and the screen opens
 * on them afterwards.
 *
 * Keys: UP/DOWN select or scroll, G1/G2 change a setting (held: repeat), START starts the scan
 * on the chosen generator (from the graph or hits page it shows the settings first), or pauses
 * its sweep and goes on (nx_bfb_pause), STOP ends a running
 * scan (the generator stops; the screen keeps what
 * it found and says where it stopped), else back; LOOP goes to the next page that has something
 * to show. The status shows the time elapsed and the time left from the pace so far. With Refine
 * on, the end of the hits' program brings the screen back for the pass on the hits
 * (NXE_BFB_REFINE, nx_ui.c): on the hits, with "Pass N", until the pass's sweep replaces them. */
#include "nx_config.h"

#if NX_FEAT_BFB
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

enum { R_GEN, R_SPEED, R_REFINE, R_AMPL, R_DWELL, R_SLOT, ROWS };
static const uint16_t k_label[ROWS] = { NX_S_DETAIL_RUN_ON, NX_S_BFB_SPEED, NX_S_BFB_REFINE, NX_S_MANUAL_AMPL,
                                        NX_S_BFB_DWELL, NX_S_BFB_SLOT };
static const uint16_t k_speed[NX_BFB_SPEEDS] = { NX_S_BFB_STANDARD, NX_S_BFB_BALANCED, NX_S_BFB_FAST };
#define VIS 4   /* rows in view: the list scrolls to the rest */

#define LIST_Y0 (NX_BAR_H + 2)
#define STATUS_Y0 (LIST_Y0 + VIS * NX_ROW_H)   /* 152: the status lines below the list */
static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };
static const nx_rect k_status = { 0, STATUS_Y0, NX_LCD_W, NX_HINT_Y };
static const nx_rect k_page = { 0, LIST_Y0, NX_LCD_W, STATUS_Y0 };   /* the list or the graph */
/* the graph page: the live strip (the last NX_BFB_TRACE readings, about 20 s, the newest at the
 * right) above the sweep strip (the whole sweep, a column per 51 steps, the hits marked) */
#define LIVE_Y0 (LIST_Y0 + 4)
#define LIVE_Y1 (LIST_Y0 + 80)
#define SWEEP_Y0 (LIST_Y0 + 88)
#define SWEEP_Y1 (STATUS_Y0 - 4)
static const nx_rect k_live = { 0, LIVE_Y0, NX_LCD_W, LIVE_Y1 };

enum { P_SET, P_GRAPH, P_HITS };

struct bfbs {
    struct nx_list l;
    uint8_t page;           /* P_* */
    uint8_t state, nhits;   /* what the status was drawn from */
    uint8_t playing;
    uint16_t step, missed, prog;
    uint32_t sig;           /* the hits drawn (hits_sig) */
    uint16_t col;           /* the graph's column of the last reading drawn */
    uint8_t scale;          /* the graph's full scale drawn */
};

static int scanning(void)
{
    uint8_t st = nx_uc_bfb()->state;
    return st != NX_BFB_IDLE && st < NX_BFB_DONE;
}

static void init_list(struct bfbs *s)
{
    uint8_t n = s->page == P_HITS ? nx_uc_bfb()->nhits : (uint8_t)ROWS;
    nx_list_init(&s->l, LIST_Y0, NX_ROW_H, VIS, n, 0, nx_ms());
}

static uint32_t hits_sig(const struct nx_bfb_view *v)
{
    uint32_t sig = v->nhits;
    for (unsigned i = 0; i < v->nhits; i++)
        sig = sig * 31u + (uint32_t)v->hits[i].code + v->hits[i].dev20;
    return sig;
}

static unsigned trace_col(unsigned step)
{
    unsigned c = (uint32_t)step * NX_BFB_TRACE / nx_uc_bfb()->steps;
    return c < NX_BFB_TRACE ? c : NX_BFB_TRACE - 1u;
}

static uint8_t graph_scale(const struct nx_bfb_view *v)   /* the largest deviation so far, at least 8 units */
{
    uint8_t a = 8;
    for (unsigned c = 0; c < NX_BFB_TRACE; c++)
        a = v->trace[c] > a ? v->trace[c] : a;
    return a;
}

static void draw_live(nx_gc *gc, const struct nx_bfb_view *v)   /* the last readings, newest at the right */
{
    const int16_t mid = (LIVE_Y0 + LIVE_Y1) / 2, half = (LIVE_Y1 - LIVE_Y0) / 2 - 1;
    unsigned n = v->live_n < NX_BFB_TRACE ? v->live_n : NX_BFB_TRACE, idx = (v->live_n - n) % NX_BFB_TRACE;
    int scale = 8;
    uint16_t col = nx_col(gc, NX_PAL_TEXT1);
    for (unsigned i = 0, k = idx; i < n; i++, k = k + 1u == NX_BFB_TRACE ? 0u : k + 1u) {
        int a = v->live[k] < 0 ? -v->live[k] : v->live[k];
        scale = a > scale ? a : scale;
    }
    nx_fill(gc, (nx_rect){ NX_M, mid, NX_M + NX_BFB_TRACE, mid + 1 }, nx_col(gc, NX_PAL_TRACK));
    int16_t prev = mid;
    for (unsigned i = 0, k = idx; i < n; i++, k = k + 1u == NX_BFB_TRACE ? 0u : k + 1u) {   /* joined: each
                                                                       * column spans to the reading before */
        int16_t y = (int16_t)(mid - v->live[k] * half / scale), x = (int16_t)(NX_M + NX_BFB_TRACE - n + i);
        int16_t y0 = i && prev < y ? prev : y, y1 = i && prev > y ? prev : y;
        nx_fill(gc, (nx_rect){ x, y0, (int16_t)(x + 1), (int16_t)(y1 + 1) }, col);
        prev = y;
    }
    uint16_t acc = nx_col(gc, nx_accent(v->ch));
    for (unsigned i = 0; i < v->nhits; i++) {   /* a mark above each hit whose peak is in view, a line down to it */
        unsigned back = (uint16_t)(v->live_n - v->hits[i].live);   /* readings since the peak */
        if (back && back <= n) {
            int16_t x = (int16_t)(NX_M + NX_BFB_TRACE - back);
            int16_t y = (int16_t)(mid - v->live[v->hits[i].live % NX_BFB_TRACE] * half / scale);
            nx_fill(gc, (nx_rect){ (int16_t)(x - 1), LIVE_Y0, (int16_t)(x + 2), LIVE_Y0 + 3 }, acc);
            if (y > LIVE_Y0 + 3)
                nx_fill(gc, (nx_rect){ x, LIVE_Y0 + 3, (int16_t)(x + 1), y }, acc);
        }
    }
}

static void draw_sweep(nx_gc *gc, const struct nx_bfb_view *v)   /* the whole sweep so far, the hits marked */
{
    const int16_t base = SWEEP_Y1 - 1, h = SWEEP_Y1 - SWEEP_Y0 - 5;
    int scale = graph_scale(v);
    unsigned cur = trace_col(v->step);
    uint16_t acc = nx_col(gc, nx_accent(v->ch)), col = nx_col(gc, NX_PAL_TEXT2);
    nx_fill(gc, (nx_rect){ NX_M, base, NX_M + NX_BFB_TRACE, base + 1 }, nx_col(gc, NX_PAL_TRACK));
    for (unsigned c = 0; c <= cur && c < NX_BFB_TRACE; c++) {
        int16_t y0 = (int16_t)(base - v->trace[c] * h / scale);
        nx_fill(gc, (nx_rect){ (int16_t)(NX_M + c), c == cur ? SWEEP_Y0 + 4 : y0, (int16_t)(NX_M + c + 1u), base }, c == cur ? acc : col);
    }
    for (unsigned i = 0; i < v->nhits; i++) {   /* a mark above each hit */
        int16_t x = (int16_t)(NX_M + trace_col(v->hits[i].step));
        nx_fill(gc, (nx_rect){ (int16_t)(x - 1), SWEEP_Y0, (int16_t)(x + 2), SWEEP_Y0 + 3 }, acc);
    }
}

static void draw_graph(nx_gc *gc, const struct nx_bfb_view *v)
{
    draw_live(gc, v);
    draw_sweep(gc, v);
}

static void row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel_ms;
    const struct bfbs *s = ctx;
    const struct nx_bfb_cfg *c = nx_uc_bfb_cfg();
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2), rx = NX_LCD_W - NX_M - (sel && s->page == P_SET ? 16 : 0);
    char v[32];
    const char *t = v;
    uint16_t col = nx_col(gc, NX_PAL_TEXT1);
    if (s->page == P_HITS) {   /* "41 010.25 Hz" and the deviation */
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
    case R_SPEED:
        t = nx_str(k_speed[c->speed < NX_BFB_SPEEDS ? c->speed : 0u]);
        break;
    case R_REFINE:
        t = nx_str(c->refine ? NX_S_COMMON_ON : NX_S_COMMON_OFF);
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

static void bar(nx_gc *gc, unsigned ch, uint32_t n, uint32_t of)   /* the progress bar, n of of */
{
    int16_t x1 = (int16_t)(NX_M + (NX_LCD_W - 2 * NX_M) * n / of);
    nx_fill(gc, (nx_rect){ NX_M, STATUS_Y0 + 37, NX_LCD_W - NX_M, STATUS_Y0 + 41 }, nx_col(gc, NX_PAL_TRACK));
    nx_fill(gc, (nx_rect){ NX_M, STATUS_Y0 + 37, x1, STATUS_Y0 + 41 }, nx_col(gc, nx_accent(ch)));
}

static void status(nx_gc *gc)
{
    const struct nx_bfb_view *v = nx_uc_bfb();
    char a[3][12], buf[64], f[24];
    const char *args[3] = { a[0], a[1], a[2] };
    uint16_t sid = NX_S_BFB_IDLE;
    uint8_t tok = NX_PAL_TEXT2;
    int sweep = v->state == NX_BFB_SWEEP || v->state == NX_BFB_SAVE;
    nx_fmt_u(a[0], v->step);
    nx_fmt_u(a[1], v->steps);
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
        sid = v->paused ? NX_S_BFB_PAUSED : NX_S_BFB_SCAN;
        if (v->paused)
            tok = NX_PAL_WARNING;
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
    case NX_BFB_STOPPED:
        sid = NX_S_BFB_STOPPED;
        tok = NX_PAL_WARNING;
        break;
    default: {   /* idle: the minutes a sweep takes at the speed chosen */
        unsigned sp = nx_uc_bfb_cfg()->speed;
        nx_fmt_u(a[0], (NX_BFB_SPEED_STEPS(sp) * NX_BFB_SPEED_MS(sp) + 30000u) / 60000u);
        break;
    }
    }
    nx_str_format(buf, sizeof buf, sid, args, 3);
    nx_text(gc, &nx_font_cap13, NX_M + 4, STATUS_Y0 + 2, buf, nx_col(gc, tok));
    if (v->pass > 1u) {   /* a refining pass, or the hits of one */
        nx_fmt_u(a[0], v->pass);
        nx_str_format(buf, sizeof buf, NX_S_BFB_PASS, args, 1);
        nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, STATUS_Y0 + 2, buf, nx_col(gc, NX_PAL_TEXT2));
    }
    if (v->state == NX_BFB_DONE && v->playing && nx_uc_bfb_cfg()->refine) {   /* the next pass follows the program */
        nx_fmt_u(a[0], v->pass + 1u);
        nx_str_format(buf, sizeof buf, NX_S_BFB_REFINES, args, 1);
        nx_text(gc, &nx_font_cap13, NX_M + 4, STATUS_Y0 + 19, buf, nx_col(gc, NX_PAL_TEXT2));
    }
    int freq = sweep || (v->state == NX_BFB_STOPPED && v->step);
    if (v->state == NX_BFB_RAMP || v->state == NX_BFB_BASE)   /* the stage's progress */
        bar(gc, v->ch, v->prog, v->state == NX_BFB_RAMP ? NX_BFB_RAMP_STEPS : NX_BFB_BASELINE);
    if (freq) {   /* the frequency, the reading, the progress: the sweep's, or how far a stopped one came */
        char *p = nx_fmt_freq(f, v->code, 2, 2, 0);
        strcpy(p, " Hz");
        nx_text(gc, &nx_font_body16, NX_M + 4, STATUS_Y0 + 19, f, nx_col(gc, NX_PAL_TEXT1));
        nx_fmt_u(f, v->reading);
        nx_text_r(gc, &nx_font_body16, NX_LCD_W - NX_M, STATUS_Y0 + 19, f, nx_col(gc, NX_PAL_TEXT2));
        bar(gc, v->ch, v->step, v->steps);
        if (sweep) {   /* the time elapsed, and left: the steps to go at the pace so far (ms per step; the
                        * nominal pace before the first) */
            uint32_t el = (v->paused ? v->pause_ms : nx_ms()) - v->sweep_ms;   /* still while paused */
            uint32_t per = v->step ? el / v->step : NX_BFB_SPEED_MS(nx_uc_bfb_cfg()->speed);
            nx_fmt_dur(a[0], el / 1000u);
            nx_str_format(buf, sizeof buf, NX_S_BFB_ELAPSED, args, 1);
            nx_text(gc, &nx_font_cap13, NX_M + 4, STATUS_Y0 + 44, buf, nx_col(gc, NX_PAL_TEXT2));
            nx_fmt_dur(a[0], per * (v->steps - v->step) / 1000u);
            nx_str_format(buf, sizeof buf, NX_S_BFB_LEFT, args, 1);
            nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, STATUS_Y0 + 44, buf, nx_col(gc, NX_PAL_TEXT2));
        }
    }
    if (v->missed) {   /* on the frequency's line, left of the reading */
        nx_fmt_u(a[0], v->missed);
        nx_str_format(buf, sizeof buf, NX_S_BFB_MISSED, args, 1);
        nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M - (freq ? 64 : 0), STATUS_Y0 + 22, buf, nx_col(gc, NX_PAL_WARNING));
    }
}

static void bfb_enter(void *st, uint32_t arg)   /* arg: 0 from the menu; 1 + ch from that generator's dashboard */
{
    struct bfbs *s = st;
    const struct nx_bfb_view *v = nx_uc_bfb();
    if (arg && !scanning())
        nx_uc_bfb_cfg()->ch = (uint8_t)(arg - 1u);
    s->page = v->state == NX_BFB_SWEEP || v->state == NX_BFB_SAVE ? P_GRAPH : v->nhits ? P_HITS
            : v->step ? P_GRAPH : P_SET;   /* a sweep that ran: its graph */
    s->state = v->state;
    s->col = 0xFFFFu;
    init_list(s);
}

static void bfb_draw(nx_gc *gc, const void *st)
{
    const struct bfbs *s = st;
    nx_comp_title_target(gc, nx_str(NX_S_BFB_TITLE), NX_S_DETAIL_RUN_ON, nx_uc_bfb_cfg()->ch);
    if (s->page == P_GRAPH)
        draw_graph(gc, nx_uc_bfb());
    else
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
    case R_GEN:   /* G1 picks G1, G2 picks G2 */
        c->ch = (uint8_t)up;
        nx_inval(k_title);
        break;
    case R_SPEED:   /* the idle line's minutes follow */
        c->speed = (uint8_t)step_by(c->speed, 1u, up, 0u, NX_BFB_SPEEDS - 1u);
        nx_inval(k_status);
        break;
    case R_REFINE:   /* the done line follows */
        c->refine ^= 1u;
        nx_inval(k_status);
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
        if ((press || e->type == NXK_REPEAT) && s->page == P_SET && !scanning())
            change(s, e->keys == NX_KEY_G2);
        return 1;
    case NX_KEY_LOOP:   /* the next page with something on it */
        if (press) {
            const struct nx_bfb_view *v = nx_uc_bfb();
            uint8_t p = s->page;
            do
                p = (uint8_t)((p + 1u) % 3u);
            while ((p == P_HITS && !v->nhits) || (p == P_GRAPH && !v->step));
            if (p != s->page) {
                s->page = p;
                init_list(s);
                nx_inval(k_page);
            }
        }
        return 1;
    case NX_KEY_START:
        if (!press)
            return 1;
        if (scanning()) {   /* the sweep paused, or on again */
            nx_uc_bfb_pause();
            nx_inval(k_status);
        } else if (s->page != P_SET) {   /* a result on show: its settings first, so a stray press scans nothing */
            s->page = P_SET;
            init_list(s);
            nx_inval(k_page);
        } else if (nx_uc_bfb_start() == NX_BUSY) {   /* the other generator has the slot */
            nx_ui_toast_s(NX_I20_WARNING, NX_PAL_WARNING, NX_S_SLOT_IN_USE, 0, 0);
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
    uint32_t sig = hits_sig(v);
    nx_list_tick(&s->l, now);
    if (v->state == NX_BFB_DONE && s->state != NX_BFB_DONE && v->nhits) {   /* the hits play: the dashboard, as a
                                                                              * loaded program (the hits stay here) */
        char a0[4], a1[4];
        nx_fmt_u(a0, v->nhits);
        nx_fmt_u(a1, v->slot);
        nx_ui_set_focus(v->ch);
        nx_ui_reset(&nx_scr_dash, 0, NX_TR_BACK);
        nx_ui_toast_s(NX_I20_CHECK_CIRCLE, nx_accent(v->ch), NX_S_BFB_SAVED, a0, a1);
        return;
    }
    if (v->state != s->state) {   /* a scan: its settings, then the graph, then its hits */
        uint8_t p = v->state == NX_BFB_SWEEP ? P_GRAPH : v->state == NX_BFB_DONE && v->nhits ? P_HITS
                  : v->state == NX_BFB_AUTH ? P_SET : s->page;
        if (p != s->page) {
            s->page = p;
            init_list(s);
            nx_inval(k_page);
        }
    }
    if (s->page == P_GRAPH && v->step != s->step) {   /* a reading: the live strip scrolls; the sweep strip's
                                                       * column, or all of it on a new column or a larger scale */
        unsigned c = trace_col(v->step);
        if (c != s->col || v->trace[c] > s->scale) {
            s->col = (uint16_t)c;
            s->scale = graph_scale(v);
            nx_inval(k_page);
        } else {
            nx_inval(k_live);
            nx_inval((nx_rect){ (int16_t)(NX_M + c), SWEEP_Y0, (int16_t)(NX_M + c + 1u), SWEEP_Y1 });
        }
    }
    if (v->state != s->state || v->step != s->step || sig != s->sig || v->playing != s->playing
        || v->missed != s->missed || v->prog != s->prog) {
        if (sig != s->sig && s->page != P_SET) {   /* a hit came, a stronger one replaced one, or the settle dropped
                                                    * them: the list anew, the sweep strip's marks */
            if (s->page == P_HITS)
                init_list(s);
            nx_inval(k_page);
        }
        s->state = v->state;
        s->playing = v->playing;
        s->step = v->step;
        s->missed = v->missed;
        s->prog = v->prog;
        s->nhits = v->nhits;
        s->sig = sig;
        nx_inval(k_status);
    }
}

const struct nx_scr nx_scr_bfb = {
    bfb_enter, bfb_key, bfb_tick, bfb_draw, NX_S_HINT_BFB, NX_SF_AWAKE, NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2,
    0, sizeof(struct bfbs),
};
#endif
