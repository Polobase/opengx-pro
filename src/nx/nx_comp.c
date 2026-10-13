/* nx UI components (nx_comp.h). */
#include "nx_comp.h"

#include "nx_anim.h"

uint8_t nx_accent(unsigned ch)
{
    return ch ? NX_PAL_G2 : NX_PAL_G1;
}

/* ---- text helpers ---- */

void nx_text_c(nx_gc *gc, const nx_font_t *f, int16_t cx, int16_t y, const char *s, uint16_t c)
{
    nx_text(gc, f, (int16_t)(cx - (int16_t)nx_font_text_width(f, s) / 2), y, s, c);
}

int16_t nx_text_r(nx_gc *gc, const nx_font_t *f, int16_t x1, int16_t y, const char *s, uint16_t c)
{
    int16_t x = (int16_t)(x1 - (int16_t)nx_font_text_width(f, s));
    nx_text(gc, f, x, y, s, c);
    return x;
}

int16_t nx_marquee_at(int16_t text_w, int16_t box_w, uint32_t sel_ms)
{
    return nx_ui_motion() && g_uiset.marquee ? nx_marquee_offset(text_w, box_w, sel_ms) : 0;
}

void nx_marquee_tick(int16_t *last, int16_t text_w, int16_t box_w, uint32_t sel_ms, nx_rect box)
{
    int16_t off = nx_marquee_at(text_w, box_w, sel_ms);
    if (off != *last) {
        *last = off;
        nx_inval(box);
    }
}

void nx_text_box(nx_gc *gc, const nx_font_t *f, nx_rect box, const char *s, uint16_t c, int focused,
                 uint32_t sel_ms)
{
    int16_t off = focused ? nx_marquee_at((int16_t)nx_font_text_width(f, s), (int16_t)(box.x1 - box.x0), sel_ms) : 0;
    if (off)
        nx_text_marquee(gc, f, box, s, off, c);
    else   /* at rest: cut with "…" (never through a glyph) */
        nx_text_fit(gc, f, box.x0, box.y0, (int16_t)(box.x1 - box.x0), s, c);
}

/* ---- bars ---- */

void nx_comp_title(nx_gc *gc, const char *title, int16_t x1, int back)
{
    nx_fill(gc, (nx_rect){ 0, 0, NX_LCD_W, NX_BAR_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_fill(gc, (nx_rect){ 0, NX_BAR_H, NX_LCD_W, NX_BAR_H + 1 }, nx_col(gc, NX_PAL_OUTLINE));
    int16_t x = NX_M;
    if (back) {   /* "‹": STOP goes back */
        static const char k_back[] = { (char)NX_CH_lsaquo, 0 };
        x = (int16_t)(nx_text(gc, &nx_font_body16, x, 1, k_back, nx_col(gc, NX_PAL_TEXT3)) + 6);
    }
    nx_text_fit(gc, &nx_font_body16, x, 1, (int16_t)(x1 - x), title, nx_col(gc, NX_PAL_TEXT1));
}

void nx_comp_hints(nx_gc *gc, uint16_t sid)
{
    nx_fill(gc, (nx_rect){ 0, NX_HINT_Y, NX_LCD_W, NX_LCD_H }, nx_col(gc, NX_PAL_SURFACE1));
    nx_fill(gc, (nx_rect){ 0, NX_HINT_Y - 1, NX_LCD_W, NX_HINT_Y }, nx_col(gc, NX_PAL_OUTLINE));
    nx_text_fit(gc, &nx_font_cap13, NX_M, NX_HINT_Y + 3, NX_LCD_W - 2 * NX_M, nx_str(sid), nx_col(gc, NX_PAL_TEXT2));
}

int16_t nx_comp_badge(nx_gc *gc, int16_t x, int16_t y, unsigned ch, int filled)
{
    nx_rect r = { x, y, (int16_t)(x + 30), (int16_t)(y + 18) };
    uint16_t acc = nx_col(gc, nx_accent(ch));
    if (filled)
        nx_pill(gc, r, acc);
    else
        nx_rrect_line(gc, r, 9, 1, nx_col(gc, NX_PAL_OUTLINE));
    nx_text_c(gc, &nx_font_cap13, (int16_t)(x + 15), (int16_t)(y - 1), nx_str(ch ? NX_S_GEN_G2 : NX_S_GEN_G1),
              filled ? nx_col(gc, NX_PAL_ON_ACCENT) : nx_col(gc, NX_PAL_TEXT2));
    return r.x1;
}

void nx_comp_title_target(nx_gc *gc, const char *title, uint16_t label, unsigned target)
{
    /* two 30 px badges 4 px apart end at the right margin, the label 6 px before them */
    const char *l = nx_str(label);
    int16_t x = NX_LCD_W - NX_M - 64, lx = (int16_t)(x - 6 - (int16_t)nx_font_text_width(&nx_font_cap13, l));
    nx_comp_title(gc, title, (int16_t)(lx - 8), 1);
    nx_comp_badge(gc, x, 2, 0, target == 0);
    nx_comp_badge(gc, (int16_t)(x + 34), 2, 1, target == 1);
    nx_text(gc, &nx_font_cap13, lx, 1, l, nx_col(gc, NX_PAL_TEXT2));
}

/* ---- list ---- */

void nx_list_init(struct nx_list *l, int16_t y0, uint8_t row_h, uint8_t rows, uint8_t n, uint8_t sel, uint32_t now)
{
    l->y0 = y0;
    l->row_h = row_h;
    l->rows = rows;
    l->n = n;
    l->anim = NX_LA_NONE;
    l->filled = 0;
    l->top = 0;
    nx_list_select(l, sel, now);
}

void nx_list_select(struct nx_list *l, uint8_t sel, uint32_t now)
{
    if (l->n == 0)
        sel = 0;
    else if (sel >= l->n)
        sel = (uint8_t)(l->n - 1u);
    l->sel = sel;
    if (sel < l->top)
        l->top = sel;
    if (sel >= l->top + l->rows)
        l->top = (uint8_t)(sel - l->rows + 1u);
    if (l->n > l->rows && l->top > l->n - l->rows)
        l->top = (uint8_t)(l->n - l->rows);
    l->anim = NX_LA_NONE;
    l->t0 = now;
    nx_inval(nx_list_view(l));
}

nx_rect nx_list_view(const struct nx_list *l)
{
    return (nx_rect){ 0, l->y0, NX_LCD_W, (int16_t)(l->y0 + l->rows * l->row_h) };
}

nx_rect nx_list_row(const struct nx_list *l, unsigned i)
{
    int16_t y = (int16_t)(l->y0 + ((int)i - (int)l->top) * l->row_h);
    return (nx_rect){ 0, y, NX_LCD_W, (int16_t)(y + l->row_h) };
}

int nx_list_key(struct nx_list *l, const struct nx_kev *e, uint32_t now)
{
    if (e->type != NXK_PRESS && e->type != NXK_REPEAT)
        return 0;
    int d = e->keys == NX_KEY_UP ? -1 : e->keys == NX_KEY_DOWN ? 1 : 0;
    if (!d || l->n == 0 || (d < 0 && l->sel == 0) || (d > 0 && l->sel + 1u >= l->n))
        return 0;
    uint8_t old = l->sel, old_top = l->top;
    nx_rect a = nx_list_row(l, old);
    l->sel = (uint8_t)(l->sel + d);
    if (l->sel < l->top)
        l->top = l->sel;
    if (l->sel >= l->top + l->rows)
        l->top = (uint8_t)(l->sel - l->rows + 1u);
    l->t0 = now;
    if (e->type == NXK_REPEAT || nx_anim_reduced()) {   /* held keys keep up: no animation */
        l->anim = NX_LA_NONE;
        if (l->top != old_top)
            nx_inval(nx_list_view(l));
        else
            nx_inval(nx_rect_or(a, nx_list_row(l, l->sel)));
        return 1;
    }
    if (l->top != old_top) {
        l->anim = NX_LA_SCROLL;
        l->from = old_top;
        nx_inval(nx_list_view(l));
    } else {
        l->anim = NX_LA_SLIDE;
        l->from = old;
        nx_inval(nx_rect_or(a, nx_list_row(l, l->sel)));
    }
    return 1;
}

void nx_list_tick(struct nx_list *l, uint32_t now)
{
    if (l->anim == NX_LA_NONE)
        return;
    uint32_t dur = l->anim == NX_LA_SLIDE ? NX_SLIDE_MS : NX_SCROLL_MS;
    if (l->anim == NX_LA_SLIDE)
        nx_inval(nx_rect_or(nx_list_row(l, l->from), nx_list_row(l, l->sel)));
    else
        nx_inval(nx_list_view(l));
    if (now - l->t0 >= dur)
        l->anim = NX_LA_NONE;   /* this invalidation draws the final frame */
}

void nx_list_draw(nx_gc *gc, const struct nx_list *l, uint8_t accent, nx_row_fn fn, const void *ctx)
{
    nx_rect view = nx_list_view(l);
    uint32_t now = nx_ui_now(), sel_ms = now - l->t0;
    int16_t off = 0, hy;
    if (l->anim == NX_LA_SCROLL) {   /* the content comes from where it was */
        uint16_t p = nx_anim_p(l->t0, now, NX_SCROLL_MS, NX_EASE_OUT);
        int32_t d = ((int32_t)l->from - (int32_t)l->top) * l->row_h;
        off = (int16_t)(-(d * (65535 - (int32_t)p)) / 65535);
    }
    hy = nx_list_row(l, l->sel).y0;
    if (l->anim == NX_LA_SLIDE) {
        uint16_t p = nx_anim_p(l->t0, now, NX_SLIDE_MS, NX_EASE_OUT);
        int16_t fy = nx_list_row(l, l->from).y0;
        hy = (int16_t)(fy + (((int32_t)(hy - fy) * p) >> 16));
        if (p == 65535u)
            hy = nx_list_row(l, l->sel).y0;
    }
    if (l->filled) {   /* the box, drawn before the clip: its frame lies 2 px outside the rows */
        nx_rect box = { 4, (int16_t)(view.y0 - 2), 316, (int16_t)(view.y1 + 2) };
        nx_rrect(gc, box, 10, nx_col(gc, NX_PAL_SURFACE1));
        nx_rrect_line(gc, box, 10, 1, nx_col(gc, NX_PAL_OUTLINE));
    }
    nx_rect saved = nx_clip_push(gc, view);
    if (l->n) {
        nx_rect bar = { 4, (int16_t)(hy + 1), 316, (int16_t)(hy + l->row_h - 1) };
        if (l->filled) {
            bar.x0 = 8;
            bar.x1 = 312;
            nx_rrect(gc, bar, 8, nx_col(gc, accent));
        } else {
            nx_rrect(gc, bar, 8, nx_col(gc, NX_PAL_SURFACE2));
            nx_fill(gc, (nx_rect){ 4, (int16_t)(hy + 5), 7, (int16_t)(hy + l->row_h - 5) }, nx_col(gc, accent));
        }
    }
    int first = (int)l->top - 1, last = (int)l->top + l->rows + 1;
    for (int i = first < 0 ? 0 : first; i < last && i < (int)l->n; i++) {
        nx_rect r = nx_list_row(l, (unsigned)i);
        r.y0 = (int16_t)(r.y0 + off);
        r.y1 = (int16_t)(r.y1 + off);
        if (r.y1 <= view.y0 || r.y0 >= view.y1)
            continue;
        int on = i == l->sel;
        if (l->filled) {   /* a gliding bar: the row it mostly covers takes the on-accent colours */
            int d = r.y0 - hy;
            on = (d < 0 ? -d : d) * 2 < l->row_h;
        }
        fn(gc, ctx, (unsigned)i, r, on, sel_ms);
    }
    if (l->n > l->rows) {   /* scrollbar: 2 px thumb at the right edge */
        int16_t h = (int16_t)(view.y1 - view.y0), th = (int16_t)(h * l->rows / l->n);
        if (th < 8)
            th = 8;
        int16_t ty = (int16_t)(view.y0 + (h - th) * l->top / (l->n - l->rows));
        int16_t sx = l->filled ? 317 : 316;   /* outside the box's frame */
        nx_fill(gc, (nx_rect){ sx, ty, (int16_t)(sx + 2), (int16_t)(ty + th) }, nx_col(gc, NX_PAL_OUTLINE));
    }
    nx_clip_pop(gc, saved);
}

/* ---- form rows ---- */

void nx_form_row(nx_gc *gc, nx_rect r, const char *label, const char *value, uint8_t kind, int on, int sel)
{
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2);
    int16_t x1 = (int16_t)(r.x1 - NX_M);
    uint16_t c2 = nx_col(gc, NX_PAL_TEXT2);
    if (kind == NX_ROW_LINK) {
        static const char k_next[] = { (char)NX_CH_rsaquo, 0 };
        x1 = (int16_t)(nx_text_r(gc, &nx_font_body16, x1, ty, k_next, nx_col(gc, NX_PAL_TEXT3)) - 8);
    }
    if (kind == NX_ROW_TOGGLE) {   /* switch: track pill, knob right when on */
        nx_rect t = { (int16_t)(x1 - 36), (int16_t)(ty + 1), x1, (int16_t)(ty + 19) };
        nx_pill(gc, t, nx_col(gc, on ? NX_PAL_FOCUS : NX_PAL_TRACK));
        int16_t kx = on ? (int16_t)(t.x1 - 16) : (int16_t)(t.x0 + 2);
        nx_disc(gc, (kx + 7) * 16, (ty + 10) * 16, 7 * 16, nx_col(gc, on ? NX_PAL_ON_ACCENT : NX_PAL_TEXT2));
        x1 = (int16_t)(t.x0 - 8);
    }
    int16_t lx = (int16_t)(r.x0 + NX_M + 4);
    if (value && *value) {
        if (kind == NX_ROW_VALUE && sel) {   /* -/+ with G1 / G2 */
            static const char k_l[] = { (char)NX_CH_lsaquo, 0 }, k_r[] = { (char)NX_CH_rsaquo, 0 };
            uint16_t ca = nx_col(gc, NX_PAL_FOCUS);
            x1 = nx_text_r(gc, &nx_font_body16, x1, ty, k_r, ca);
            x1 = nx_text_r(gc, &nx_font_body16, (int16_t)(x1 - 6), ty, value, nx_col(gc, NX_PAL_TEXT1));
            x1 = nx_text_r(gc, &nx_font_body16, (int16_t)(x1 - 6), ty, k_l, ca);
        } else {   /* right-aligned; a value too long ends in "…" */
            nx_fit_t fit = nx_font_fit(&nx_font_body16, value, (unsigned)(x1 - lx - 90));
            x1 = (int16_t)(x1 - fit.width);
            int16_t vx = nx_text_n(gc, &nx_font_body16, x1, ty, value, fit.n, c2);
            if (fit.ellipsis)
                nx_text(gc, &nx_font_body16, vx, ty, nx_font_ellipsis(&nx_font_body16), c2);
        }
    }
    nx_text_fit(gc, &nx_font_body16, lx, ty, (int16_t)(x1 - lx - 8), label,
                 nx_col(gc, kind == NX_ROW_INFO && !value ? NX_PAL_TEXT2 : NX_PAL_TEXT1));
}

void nx_form_bar(nx_gc *gc, nx_rect r, const char *label, uint16_t frac, const char *value, int sel)
{
    int16_t ty = (int16_t)(r.y0 + (r.y1 - r.y0 - 20) / 2);
    int16_t lx = (int16_t)(r.x0 + NX_M + 4), x1 = (int16_t)(r.x1 - NX_M);
    int16_t vx = nx_text_r(gc, &nx_font_body16, x1, ty, value, nx_col(gc, sel ? NX_PAL_TEXT1 : NX_PAL_TEXT2));
    int16_t lw = (int16_t)nx_font_text_width(&nx_font_body16, label);
    nx_text(gc, &nx_font_body16, lx, ty, label, nx_col(gc, NX_PAL_TEXT1));
    int16_t bx0 = (int16_t)(lx + lw + 12), bx1 = (int16_t)(vx - 10);
    if (bx1 > bx0 + 20)
        nx_bar(gc, (nx_rect){ bx0, (int16_t)(ty + 7), bx1, (int16_t)(ty + 13) }, frac,
               nx_col(gc, sel ? NX_PAL_FOCUS : NX_PAL_TEXT2), nx_col(gc, NX_PAL_TRACK));
}

/* ---- sheets, cards, rings ---- */

void nx_sheet_frame(nx_gc *gc, int16_t y0)
{
    nx_rrect(gc, (nx_rect){ 0, y0, NX_LCD_W, (int16_t)(NX_LCD_H + 14) }, 14, nx_col(gc, NX_PAL_SURFACE1));
    nx_rrect_line(gc, (nx_rect){ 0, y0, NX_LCD_W, (int16_t)(NX_LCD_H + 14) }, 14, 1, nx_col(gc, NX_PAL_OUTLINE));
    nx_fill(gc, (nx_rect){ 144, (int16_t)(y0 + 5), 176, (int16_t)(y0 + 8) }, nx_col(gc, NX_PAL_OUTLINE));
}

void nx_grid_cell(nx_gc *gc, nx_rect r, uint8_t icon, const char *label, int sel)
{
    uint16_t c = nx_col(gc, sel ? NX_PAL_TEXT1 : NX_PAL_TEXT2);
    if (sel) {
        nx_rrect(gc, r, 10, nx_col(gc, NX_PAL_SURFACE2));
        nx_rrect_line(gc, r, 10, 2, nx_col(gc, NX_PAL_FOCUS));
    }
    int16_t cx = (int16_t)((r.x0 + r.x1) / 2);
    nx_icon(gc, &nx_icons20, icon, (int16_t)(cx - 10), (int16_t)(r.y0 + 7), c);
    nx_fit_t f = nx_font_fit(&nx_font_cap13, label, (unsigned)(r.x1 - r.x0 - 4));
    int16_t x = nx_text_n(gc, &nx_font_cap13, (int16_t)(cx - f.width / 2), (int16_t)(r.y0 + 30), label, f.n, c);
    if (f.ellipsis)
        nx_text(gc, &nx_font_cap13, x, (int16_t)(r.y0 + 30), nx_font_ellipsis(&nx_font_cap13), c);
}

void nx_card(nx_gc *gc, nx_rect r)
{
    nx_rrect(gc, r, 12, nx_col(gc, NX_PAL_SURFACE1));
    nx_rrect_line(gc, r, 12, 1, nx_col(gc, NX_PAL_OUTLINE));
}

void nx_hold_ring(nx_gc *gc, int16_t cx, int16_t cy, uint8_t r, uint8_t w, uint16_t frac, uint8_t tok)
{
    nx_ring(gc, cx, cy, r, w, 0, 1024, 0, nx_col(gc, NX_PAL_TRACK));
    if (frac)
        nx_ring(gc, cx, cy, r, w, 0, (uint16_t)(((uint32_t)frac * 1024u) >> 16), 1, nx_col(gc, tok));
}
