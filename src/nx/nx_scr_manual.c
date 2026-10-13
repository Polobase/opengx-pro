/* The manual generator (menu > Manual, or LOOP held on its dashboard; NX_FEAT_MANUAL,
 * nx_manual.h): its values as a list; changes apply at once, also while it plays.
 *
 * Keys: UP/DOWN select, G1/G2 change the value (held: repeat), LOOP on a frequency the digit that
 * G1/G2 change (0.01 Hz, 0.1 Hz, ... 10 MHz, then round again), START plays it on the chosen
 * generator (while it plays there: stops it), STOP back. */
#include "nx_config.h"

#if NX_FEAT_MANUAL
#include <string.h>

#include "nx_comp.h"
#include "nx_uifmt.h"

static const nx_rect k_title = { 0, 0, NX_LCD_W, NX_BAR_H };

enum { R_GEN, R_MODE, R_FREQ, R_START, R_END, R_TIME, R_SHAPE, R_REPEAT, R_WAVE, R_AMPL };
static const uint8_t k_fixed[] = { R_GEN, R_MODE, R_FREQ, R_WAVE, R_AMPL };
static const uint8_t k_sweep[] = { R_GEN, R_MODE, R_START, R_END, R_TIME, R_SHAPE, R_REPEAT, R_WAVE, R_AMPL };
static const uint16_t k_label[] = {
    NX_S_DETAIL_RUN_ON, NX_S_MANUAL_MODE, NX_S_MANUAL_FREQ, NX_S_MANUAL_START, NX_S_MANUAL_END,
    NX_S_MANUAL_TIME,   NX_S_MANUAL_SHAPE, NX_S_MANUAL_REPEAT, NX_S_MANUAL_WAVE, NX_S_MANUAL_AMPL,
};
static const uint32_t k_time_step[4][2] = { { 60u, 1u }, { 300u, 5u }, { 3600u, 30u }, { 86400u, 300u } };

struct manual {
    struct nx_list l;
    uint8_t target, digit;   /* digit: 0 = 0.01 Hz ... 9 = 10 MHz */
    uint8_t playing;         /* the target plays it (for the redraw) */
};

static const uint8_t *rows_of(uint8_t *n)
{
    int sweep = nx_uc_manual()->mode == NX_MAN_SWEEP;
    *n = sweep ? (uint8_t)sizeof k_sweep : (uint8_t)sizeof k_fixed;
    return sweep ? k_sweep : k_fixed;
}

static uint32_t *freq_of(uint8_t row)
{
    struct nx_manual *m = nx_uc_manual();
    return row == R_FREQ ? &m->f_chz : row == R_START ? &m->start_chz : row == R_END ? &m->end_chz : 0;
}

static int plays(unsigned ch)
{
    return g_vm.g[ch].slot == NX_UC_MANUAL && g_vm.g[ch].state == NXG_RUNNING;
}

/* "1000.00" with at least `digits` digits before the point */
static char *fmt_chz(char *p, uint32_t chz, unsigned digits)
{
    char n[12];
    char *e = nx_fmt_u(n, chz / 100u);
    for (unsigned k = (unsigned)(e - n); k < digits; k++)
        *p++ = '0';
    p = strcpy(p, n) + (e - n);
    *p++ = '.';
    return nx_fmt_2d(p, chz % 100u);
}

static void manual_row(nx_gc *gc, const void *ctx, unsigned i, nx_rect r, int sel, uint32_t sel_ms)
{
    (void)sel_ms;
    const struct manual *s = ctx;
    const struct nx_manual *m = nx_uc_manual();
    uint8_t n;
    uint8_t row = rows_of(&n)[i];
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2), rx = NX_LCD_W - NX_M - (sel ? 16 : 0);
    nx_text(gc, &nx_font_body16, NX_M + 4, ty, nx_str(k_label[row]), nx_col(gc, NX_PAL_TEXT2));
    char v[24];
    const char *t = v;
    uint16_t col = nx_col(gc, NX_PAL_TEXT1);
    switch (row) {
    case R_GEN:
        t = nx_str(s->target ? NX_S_GEN_G2 : NX_S_GEN_G1);
        col = nx_col(gc, nx_accent(s->target));
        if (plays(s->target))
            nx_icon(gc, &nx_icons20, NX_I20_PLAY, (int16_t)(rx - 52), ty, col);
        break;
    case R_MODE:
        t = nx_str(m->mode == NX_MAN_SWEEP ? NX_S_MANUAL_SWEEP : NX_S_MANUAL_FIXED);
        break;
    case R_FREQ:
    case R_START:
    case R_END: {
        rx = nx_text_r(gc, &nx_font_body16, rx, ty, " Hz", nx_col(gc, NX_PAL_TEXT2));
        char *e = fmt_chz(v, *freq_of(row), sel && s->digit >= 2u ? s->digit - 1u : 1u);
        if (sel) {   /* the digit G1/G2 change */
            unsigned at = (unsigned)(e - v) - 1u - s->digit - (s->digit >= 2u);
            int16_t x0 = (int16_t)(rx - nx_font_text_width(&nx_font_body16, v)
                                   + nx_font_text_width_n(&nx_font_body16, v, at));
            nx_fill(gc, (nx_rect){ x0, (int16_t)(ty + 20), (int16_t)(x0 + nx_font_text_width_n(&nx_font_body16, v + at, 1)),
                                   (int16_t)(ty + 22) }, nx_col(gc, NX_PAL_FOCUS));
        }
        break;
    }
    case R_TIME:
        nx_fmt_dur(v, m->time_s);
        break;
    case R_SHAPE:
        t = nx_str(m->shape ? NX_S_MANUAL_LOG : NX_S_MANUAL_LINEAR);
        break;
    case R_REPEAT:
        t = nx_str(m->repeat == NX_SW_ONCE ? NX_S_MANUAL_ONCE : m->repeat == NX_SW_LOOP ? NX_S_MANUAL_LOOP
                                                                                          : NX_S_MANUAL_PINGPONG);
        break;
    case R_WAVE: {
        struct nx_wave_info wi;
        nx_wave_info((uint8_t)(10u + m->wave), &wi);
        t = nx_str(wi.name);
        break;
    }
    default: {   /* R_AMPL: "5.0 V" */
        char *p = nx_fmt_u(v, m->ampl_dv / 10u);
        *p++ = '.';
        p = nx_fmt_u(p, m->ampl_dv % 10u);
        strcpy(p, " V");
        break;
    }
    }
    nx_text_r(gc, &nx_font_body16, rx, ty, t, col);
    if (sel) {   /* G1 / G2 change it */
        static const char k_lr[] = { (char)NX_CH_lsaquo, 0 };
        static const char k_rr[] = { (char)NX_CH_rsaquo, 0 };
        nx_text_r(gc, &nx_font_body16, NX_LCD_W - NX_M, ty, k_rr, nx_col(gc, NX_PAL_FOCUS));
        nx_text(gc, &nx_font_body16, 150, ty, k_lr, nx_col(gc, NX_PAL_FOCUS));
    }
}

static void manual_enter(void *st, uint32_t arg)
{
    struct manual *s = st;
    uint8_t n;
    rows_of(&n);
    s->target = (uint8_t)(arg & 0x100u ? arg & 1u : nx_ui_focus());   /* the dashboard's generator, or the focus */
    s->digit = 2;   /* 1 Hz */
    s->playing = (uint8_t)plays(s->target);
    nx_list_init(&s->l, NX_BAR_H + 2, NX_ROW_H, 6, n, 0, nx_ms());
}

static void manual_draw(nx_gc *gc, const void *st)
{
    const struct manual *s = st;
    nx_comp_title_target(gc, nx_str(NX_S_MANUAL_TITLE), NX_S_DETAIL_RUN_ON, s->target);
    nx_list_draw(gc, &s->l, nx_accent(s->target), manual_row, s);
}

static uint32_t step_by(uint32_t v, uint32_t d, int up, uint32_t lo, uint32_t hi)
{
    if (up)
        return v > hi - d ? hi : v + d;
    return v < lo + d ? lo : v - d;
}

static void change(struct manual *s, uint8_t row, int up)
{
    struct nx_manual *m = nx_uc_manual();
    uint32_t *f = freq_of(row);
    if (f) {
        uint32_t d = 1;
        for (unsigned i = 0; i < s->digit; i++)
            d *= 10u;
        *f = step_by(*f, d, up, 1u, NX_MANUAL_MAX_CHZ);
    } else {
        switch (row) {
        case R_GEN:
            s->target ^= 1u;
            nx_inval(k_title);
            return;
        case R_MODE:
            m->mode ^= 1u;
            break;
        case R_TIME: {
            unsigned k = 0;
            while (k < 3u && (up ? m->time_s >= k_time_step[k][0] : m->time_s > k_time_step[k][0]))
                k++;
            m->time_s = step_by(m->time_s, k_time_step[k][1], up, 1u, 86400u);
            break;
        }
        case R_SHAPE:
            m->shape ^= 1u;
            break;
        case R_REPEAT:
            m->repeat = (uint8_t)((m->repeat + (up ? 1u : 2u)) % 3u);
            break;
        case R_WAVE:
            m->wave = (uint8_t)((m->wave + (up ? 0u : 7u)) % 9u + 1u);
            break;
        default:
            m->ampl_dv = (uint16_t)step_by(m->ampl_dv, 1u, up, 0u, 200u);
            break;
        }
    }
    nx_uc_manual_changed();
}

static int manual_key(void *st, const struct nx_kev *e)
{
    struct manual *s = st;
    if (nx_list_key(&s->l, e, nx_ms()))
        return 1;
    uint8_t n;
    uint8_t row = rows_of(&n)[s->l.sel];
    int press = e->type == NXK_PRESS;
    switch (e->keys) {
    case NX_KEY_G1:
    case NX_KEY_G2:
        if (press || e->type == NXK_REPEAT) {
            change(s, row, e->keys == NX_KEY_G2);
            uint8_t n2;
            rows_of(&n2);
            if (n2 != n)   /* Fixed <-> Sweep: other rows */
                nx_list_init(&s->l, s->l.y0, s->l.row_h, s->l.rows, n2, s->l.sel, nx_ms());
            nx_inval(nx_list_view(&s->l));
        }
        return 1;
    case NX_KEY_LOOP:
        if (press && freq_of(row)) {
            s->digit = (uint8_t)((s->digit + 1u) % 10u);
            nx_inval(nx_list_row(&s->l, s->l.sel));
        }
        return 1;
    case NX_KEY_START:
        if (press) {
            if (plays(s->target))
                nx_ui_act(NXA_STOP, s->target, 0, 0);
            else
                nx_uc_manual_play(s->target);
        }
        return 1;
    case NX_KEY_STOP:
        if (press)
            nx_ui_pop(NX_TR_BACK);
        return 1;
    default:
        return 1;
    }
}

static void manual_tick(void *st, uint32_t now)
{
    struct manual *s = st;
    nx_list_tick(&s->l, now);
    uint8_t p = (uint8_t)plays(s->target);
    if (p != s->playing) {
        s->playing = p;
        nx_inval(nx_list_row(&s->l, 0));
    }
}

const struct nx_scr nx_scr_manual = {
    manual_enter, manual_key, manual_tick, manual_draw, NX_S_HINT_MANUAL, 0,
    NX_KEY_UP | NX_KEY_DOWN | NX_KEY_G1 | NX_KEY_G2, 0, sizeof(struct manual),
};
#endif
