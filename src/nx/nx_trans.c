/* nx transitions (nx_trans.h). */
#include "nx_trans.h"

#include "nx_anim.h"
#include "nx_bl.h"
#include "nx_render.h"
#include "nx_time.h"

enum { T_NONE = 0, T_PUSH, T_DIP, T_REVEAL, T_SLIDE, T_FADE };

static struct {
    uint8_t kind, phase, dir, layer, in;
    uint16_t steps, ms;
    int16_t dy0, dy1, dy;
    uint32_t t0;
    nx_rect area;
} s_t;
static uint8_t s_scroll_owed;   /* nx_trans_cancel's scroll back to 0 did not fit in the LCD queue */

nx_rect nx_push_cols(uint8_t dir, unsigned k)
{
    int16_t a = (int16_t)((k - 1u) * NX_PUSH_DX), b = (int16_t)(k * NX_PUSH_DX);
    if (dir == NX_PUSH_RIGHT) {
        int16_t t = (int16_t)(NX_LCD_W - b);
        b = (int16_t)(NX_LCD_W - a);
        a = t;
    }
    return (nx_rect){ a, 0, b, NX_LCD_H };
}

uint16_t nx_push_scroll(uint8_t dir, unsigned k)
{
    unsigned s = k * NX_PUSH_DX;
    return (uint16_t)((dir == NX_PUSH_RIGHT ? NX_LCD_W - s % NX_LCD_W : s) % NX_LCD_W);
}

int nx_trans_active(void)
{
    return s_t.kind != T_NONE;
}

void nx_trans_cancel(void)
{
    if (s_t.kind == T_PUSH && s_t.steps < NX_PUSH_STEPS && !nx_lcd_scroll(0))
        s_scroll_owed = 1;   /* sent by the next nx_trans_step, before anything else is drawn */
    if (s_t.kind == T_DIP && s_t.phase < 3)
        nx_bl_fade_to(nx_bl_user(), 0);   /* phase < 3: down or held at half; phase 3+ already fades up */
    if (s_t.kind == T_REVEAL)
        nx_render_layer_clip(s_t.layer, 0);
    if (s_t.kind == T_SLIDE)
        nx_render_layer_offset(s_t.layer, 0, s_t.dy1);
#if NX_ENG_FADE
    if (s_t.kind == T_FADE)
        nx_render_layer_fade(s_t.layer, s_t.in ? 32u : 0u);
#endif
    s_t.kind = T_NONE;
    nx_inval_all();
}

int nx_trans_dip(void)
{
    if (s_t.kind != T_NONE)
        return 0;
    s_t.kind = T_DIP;
    s_t.phase = 0;
    return 1;
}

int nx_trans_push(uint8_t dir)
{
    if (s_t.kind != T_NONE)
        return 0;
    if (nx_anim_reduced()) {
        nx_inval_all();
        return nx_trans_dip();
    }
    s_t.kind = T_PUSH;
    s_t.dir = dir;
    s_t.steps = 0;
    s_t.t0 = nx_ms();
    nx_render_restart();   /* the push draws the whole new screen; pending rectangles are moot */
    return 1;
}

int nx_trans_reveal(uint8_t layer, nx_rect area)
{
    if (s_t.kind != T_NONE)
        return 0;
    if (nx_anim_reduced()) {
        nx_render_layer_clip(layer, 0);
        nx_inval_all();
        return nx_trans_dip();
    }
    static const nx_rect k_none = { 0, 0, 0, 0 };
    s_t.kind = T_REVEAL;
    s_t.phase = 0;
    s_t.layer = layer;
    s_t.area = area;
    s_t.steps = 0;
    nx_render_layer_clip(layer, &k_none);   /* hidden while the scrimmed base is rendered */
    nx_inval_all();
    return 1;
}

static nx_rect moved(nx_rect r, int16_t dy)
{
    r.y0 = (int16_t)(r.y0 + dy);
    r.y1 = (int16_t)(r.y1 + dy);
    return r;
}

int nx_trans_slide(uint8_t layer, nx_rect area, int16_t dy0, int16_t dy1, uint16_t ms)
{
    if (s_t.kind != T_NONE)
        return 0;
    nx_inval(moved(area, dy0));
    if (nx_anim_reduced() || ms == 0) {
        nx_render_layer_offset(layer, 0, dy1);
        nx_inval(moved(area, dy1));
        return 1;
    }
    s_t.kind = T_SLIDE;
    s_t.layer = layer;
    s_t.area = area;
    s_t.dy0 = s_t.dy = dy0;
    s_t.dy1 = dy1;
    s_t.ms = ms;
    s_t.t0 = nx_ms();
    nx_render_layer_offset(layer, 0, dy0);
    return 1;
}

#if NX_ENG_FADE
int nx_trans_fade(uint8_t layer, nx_rect area, int in)
{
    if (s_t.kind != T_NONE)
        return 0;
    nx_inval(area);
    if (nx_anim_reduced()) {
        nx_render_layer_fade(layer, in ? 32u : 0u);
        return 1;
    }
    s_t.kind = T_FADE;
    s_t.layer = layer;
    s_t.area = area;
    s_t.in = (uint8_t)(in != 0);
    s_t.steps = 0;
    s_t.t0 = nx_ms();
    nx_render_layer_fade(layer, in ? 0u : 32u);
    return 1;
}
#endif

static int step_push(uint32_t now)
{
    uint16_t p = nx_anim_p(s_t.t0, now, NX_PUSH_MS, NX_EASE_IN_OUT);
    unsigned due = p == 65535u ? NX_PUSH_STEPS : ((uint32_t)p * NX_PUSH_STEPS) >> 16;   /* < 40 until the end */
    while (s_t.steps < due) {
        if (nx_lcd_room() < 2u)
            return 1;
        unsigned k = s_t.steps + 1u;
        nx_rect cols = nx_push_cols(s_t.dir, k);
        if (!nx_render_band(&cols))
            return 1;            /* no free buffer yet */
        nx_lcd_scroll(nx_push_scroll(s_t.dir, k));
        s_t.steps = (uint16_t)k;
    }
    if (s_t.steps >= NX_PUSH_STEPS)
        s_t.kind = T_NONE;
    return 1;
}

static int step_dip(uint32_t now)
{
    switch (s_t.phase) {
    case 0:   /* down, holding the old picture */
        nx_bl_fade_to((uint8_t)(nx_bl_user() / 2u), NX_DIP_DOWN_MS);
        s_t.t0 = now;
        s_t.phase = 1;
        return 1;
    case 1:
        if (now - s_t.t0 < NX_DIP_DOWN_MS)
            return 1;
        nx_inval_all();
        s_t.t0 = now;
        s_t.phase = 2;
        return 0;
    case 2:   /* the full render in the half light */
        if (nx_render_pending() && now - s_t.t0 < 2u * NX_LCD_FRAME_US / 1000u)
            return 0;
        nx_bl_fade_to(nx_bl_user(), NX_DIP_UP_MS);
        s_t.t0 = now;
        s_t.phase = 3;
        return 0;
    default:
        if (now - s_t.t0 >= NX_DIP_UP_MS)
            s_t.kind = T_NONE;
        return 0;
    }
}

static int step_reveal(uint32_t now)
{
    if (s_t.phase == 0) {
        if (nx_render_pending())
            return 0;            /* the base first */
        s_t.phase = 1;
        s_t.t0 = now;
    }
    int16_t h = (int16_t)(s_t.area.y1 - s_t.area.y0);
    uint16_t p = nx_anim_p(s_t.t0, now, NX_REVEAL_MS, NX_EASE_OUT);
    int16_t rows = p == 65535u ? h : (int16_t)(((int32_t)h * p) >> 16);
    if (rows > (int16_t)s_t.steps) {
        nx_rect shown = { s_t.area.x0, (int16_t)(s_t.area.y1 - rows), s_t.area.x1, s_t.area.y1 };
        nx_render_layer_clip(s_t.layer, &shown);
        nx_inval((nx_rect){ s_t.area.x0, shown.y0, s_t.area.x1, (int16_t)(s_t.area.y1 - s_t.steps) });
        s_t.steps = (uint16_t)rows;
    }
    if (rows >= h) {
        nx_render_layer_clip(s_t.layer, 0);
        s_t.kind = T_NONE;
    }
    return 0;
}

static int step_slide(uint32_t now)
{
    if (nx_render_pending())
        return 0;                /* one frame at a time */
    uint16_t p = nx_anim_p(s_t.t0, now, s_t.ms, NX_EASE_OUT);
    int16_t dy = p == 65535u ? s_t.dy1 : (int16_t)(s_t.dy0 + (((int32_t)s_t.dy1 - s_t.dy0) * p >> 16));
    if (dy != s_t.dy) {
        nx_inval(moved(s_t.area, s_t.dy));
        nx_inval(moved(s_t.area, dy));
        nx_render_layer_offset(s_t.layer, 0, dy);
        s_t.dy = dy;
    }
    if (p == 65535u)
        s_t.kind = T_NONE;
    return 0;
}

#if NX_ENG_FADE
static int step_fade(uint32_t now)
{
    if (nx_render_pending())
        return 0;
    uint32_t t = now - s_t.t0;
    unsigned k = t >= NX_FADE_MS ? NX_FADE_STEPS : t * NX_FADE_STEPS / NX_FADE_MS;
    if (k > s_t.steps) {
        unsigned level = 32u * k / NX_FADE_STEPS;
        nx_render_layer_fade(s_t.layer, (uint8_t)(s_t.in ? level : 32u - level));
        nx_inval(s_t.area);
        s_t.steps = (uint16_t)k;
    }
    if (k >= NX_FADE_STEPS)
        s_t.kind = T_NONE;
    return 0;
}
#endif

int nx_trans_step(uint32_t now)
{
    if (s_scroll_owed) {   /* nothing may be drawn at the old scroll offset: hold the frame */
        if (!nx_lcd_scroll(0))
            return 1;
        s_scroll_owed = 0;
    }
    switch (s_t.kind) {
    case T_PUSH:
        return step_push(now);
    case T_DIP:
        return step_dip(now);
    case T_REVEAL:
        return step_reveal(now);
    case T_SLIDE:
        return step_slide(now);
#if NX_ENG_FADE
    case T_FADE:
        return step_fade(now);
#endif
    default:
        return 0;
    }
}
