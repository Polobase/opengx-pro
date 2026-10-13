/* Calibration (docs/nx/design/ui.md W20): the classic grid's topology in the new look. The 29
 * values sit where the classic screen has them: 21..28 as pairs in the top two rows (one column
 * per output: G1 Out1, G1 Out2, G2 Out1, G2 Out2), 0 in the centre, 1..20 in four columns of
 * five. Every key goes to the shared calibration menu (core_calmenu through nx_uc_cal_key), so
 * the generator lines stay the original's; the factory port's values 21..24 keep the classic
 * "inverted" mark (a filled cell) until the menu selects them. nx adds a title bar with the test
 * frequency / level step exponents (LOOP and STOP change them) and two lines of key help. */
#include "nx_comp.h"
#include "nx_uifmt.h"

/* Between the title bar and the help lines: pairs from PAIR_Y, 0 at ZERO_Y, 1..20 from GRID_Y. */
#define CELL_W 76
#define PAIR_Y 28
#define ZERO_Y 76
#define GRID_Y 104
#define ROW_H 20
#define HELP_Y 204
static const nx_rect k_readout = { 120, 0, NX_LCD_W, NX_BAR_H };

struct cal {
    uint8_t marks;      /* bit idx - 21: the factory port set value idx (21..24 drawn filled) */
    uint8_t sel;        /* the selection drawn last */
};

static nx_rect cell(unsigned idx)
{
    int16_t x, y;
    if (idx == 0) {
        x = 122;
        y = ZERO_Y;
    } else if (idx <= 20u) {
        x = (int16_t)(NX_M + ((idx - 1u) / 5u) * CELL_W);
        y = (int16_t)(GRID_Y + ((idx - 1u) % 5u) * ROW_H);
    } else {
        x = (int16_t)(NX_M + ((idx - 21u) / 2u) * CELL_W);
        y = (int16_t)(PAIR_Y + ((idx - 21u) % 2u) * ROW_H);
    }
    return (nx_rect){ x, y, (int16_t)(x + CELL_W - 4), (int16_t)(y + ROW_H) };
}

static void cal_draw(nx_gc *gc, const void *st)
{
    const struct cal *c = st;
    const struct nx_sys_vm *s = &g_vm_draw.s;
    char v[8], buf[48];
    nx_fill(gc, (nx_rect){ 0, 0, NX_LCD_W, NX_BAR_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_text(gc, &nx_font_body16, NX_M, 1, nx_str(NX_S_CAL_TITLE), nx_col(gc, NX_PAL_TEXT1));
    char *p = buf;   /* "TEST FREQ. n   TEST LEVEL n" */
    for (const char *t = nx_str(NX_S_CAL_TEST_FREQ); *t;)
        *p++ = *t++;
    *p++ = ' ';
    p = nx_fmt_u(p, s->cal_fexp);
    for (const char *t = "   "; *t;)
        *p++ = *t++;
    for (const char *t = nx_str(NX_S_CAL_TEST_LEVEL); *t;)
        *p++ = *t++;
    *p++ = ' ';
    nx_fmt_u(p, s->cal_aexp);
    nx_text_r(gc, &nx_font_cap13, NX_LCD_W - NX_M, 2, buf, nx_col(gc, NX_PAL_TEXT2));
    for (unsigned idx = 0; idx < 29u; idx++) {
        nx_rect r = cell(idx);
        int sel = idx == s->cal_index, mark = idx >= 21u && idx <= 24u && ((c->marks >> (idx - 21u)) & 1u);
        if (sel)
            nx_rrect(gc, r, 6, nx_col(gc, NX_PAL_FOCUS));
        else if (mark)
            nx_rrect(gc, r, 6, nx_col(gc, NX_PAL_G1_DIM));
        nx_fmt_u(v, idx);
        nx_text_r(gc, &nx_font_cap13, (int16_t)(r.x0 + 18), (int16_t)(r.y0 + 1), v,
                  nx_col(gc, sel ? NX_PAL_ON_ACCENT : NX_PAL_TEXT3));
        int32_t val = nx_uc_cal_value(idx);
        uint32_t u = (uint32_t)(val < 0 ? 0 : val);
        for (int k = 4; k >= 0; k--) {   /* five digits, as the classic grid */
            v[k] = (char)('0' + u % 10u);
            u /= 10u;
        }
        v[5] = 0;
        nx_text(gc, &nx_font_body16, (int16_t)(r.x0 + 22), r.y0, v, nx_col(gc, sel ? NX_PAL_ON_ACCENT : NX_PAL_TEXT1));
    }
    nx_fill(gc, (nx_rect){ 0, HELP_Y, NX_LCD_W, NX_LCD_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_text_fit(gc, &nx_font_cap13, NX_M, HELP_Y + 1, 296, nx_str(NX_S_CAL_HINT1), nx_col(gc, NX_PAL_TEXT2));
    nx_text_fit(gc, &nx_font_cap13, NX_M, HELP_Y + 17, 296, nx_str(NX_S_CAL_HINT2), nx_col(gc, NX_PAL_TEXT2));
}

static int cal_key(void *st, const struct nx_kev *e)
{
    struct cal *c = st;
    if (nx_uc_cal_key(e)) {   /* a value or the selection changed */
        nx_inval(cell(c->sel));
        nx_inval(cell(g_vm.s.cal_index));
        nx_inval(k_readout);
    }
    return 1;
}

static void cal_tick(void *st, uint32_t now)
{
    (void)now;
    struct cal *c = st;
    const struct nx_sys_vm *s = &g_vm.s, *o = &g_vm_seen.s;
    if (s->cal_index != o->cal_index || s->cal_index != c->sel) {
        nx_inval(cell(c->sel));
        nx_inval(cell(s->cal_index));
        if (s->cal_index >= 21u && s->cal_index <= 24u)
            c->marks &= (uint8_t)~(1u << (s->cal_index - 21u));   /* selected: the mark is gone */
        c->sel = s->cal_index;
    }
    if (s->cal_fexp != o->cal_fexp || s->cal_aexp != o->cal_aexp)
        nx_inval(k_readout);
    if (s->cal_sum != o->cal_sum)   /* a value set by the factory port (:c) or the PC (w60/62/63/80) */
        nx_inval((nx_rect){ 0, PAIR_Y, NX_LCD_W, HELP_Y });
}

void nx_scr_cal_factory(void *st, unsigned idx)
{
    struct cal *c = st;
    if (idx >= 21u && idx <= 24u)
        c->marks |= (uint8_t)(1u << (idx - 21u));
    if (idx < 29u)
        nx_inval(cell(idx));
}

const struct nx_scr nx_scr_cal = {
    0, cal_key, cal_tick, cal_draw, NX_NONE, NX_SF_RAW | NX_SF_AWAKE, NX_KEY_G1 | NX_KEY_G2, 0, sizeof(struct cal),
};
