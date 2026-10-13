/* nx render scheduler (nx_render.h). */
#include "nx_render.h"

#include "lcd.h"
#include "nx_font.h"
#include "nx_time.h"
#include "nx_trans.h"

#define FLUSH_MAX_US 500000u  /* nx_render_flush: a few frames at most, far below the 2 s watchdog */
#define FLUSH_SPINS 400000u   /* ... and a bound that holds even if the microsecond clock stood still */
#define TEXT_CELLS 32u     /* nx_inval_text: longest text compared cell by cell */

struct layer {
    nx_draw_fn fn;
    void *ctx;
    nx_rect clip;
    int16_t dx, dy;
    uint8_t fade;
};

struct nx_render_stats g_nx_render_stats;

static const nx_rect k_screen = { 0, 0, NX_LCD_W, NX_LCD_H };
static struct layer s_layer[NX_LAYER_COUNT];
static uint8_t s_theme;
static const uint16_t *s_pal;
static uint8_t s_scrim;
#if NX_ENG_VGRAD
static uint8_t s_bg_top, s_bg_bot;
#define BG_TOP s_bg_top
#else
#define BG_TOP NX_PAL_BG   /* a solid background only (nx_config.h) */
#endif
static void (*s_on_frame)(void);

static nx_rect s_dirty[NX_DIRTY_MAX];   /* next frame */
static uint8_t s_ndirty;
static nx_rect s_cur[NX_DIRTY_MAX];     /* this frame, top to bottom */
static uint8_t s_ncur, s_icur;
static nx_rect s_rect;                  /* rectangle being banded */
static int16_t s_y, s_bh;
static uint8_t s_in_rect, s_in_frame;
static uint32_t s_frame_t0;
static nx_rect s_probed;                /* the band of the probe cache */
static int8_t s_probe = -1;             /* -1: none, 0: has content, 1: plain background */
static uint16_t s_band_us;              /* composition time of the last band */

/* ---- setup ---- */

void nx_render_init(void)
{
    for (unsigned i = 0; i < NX_LAYER_COUNT; i++)
        s_layer[i] = (struct layer){ 0, 0, k_screen, 0, 0, 32 };
    s_theme = NX_THEME_DARK;
    s_pal = nx_palettes[s_theme];
#if NX_ENG_VGRAD
    s_bg_top = s_bg_bot = NX_PAL_BG;
#endif
    s_scrim = 0;
    s_on_frame = 0;
    s_ndirty = s_ncur = s_icur = 0;
    s_in_rect = s_in_frame = 0;
    s_probe = -1;
    g_nx_render_stats = (struct nx_render_stats){ 0 };
    if (nx_trans_active())
        nx_trans_cancel();
    nx_inval_all();
}

void nx_render_theme(uint8_t theme)
{
    s_probe = -1;
    s_theme = theme < NX_THEME_COUNT ? theme : NX_THEME_DARK;
    s_pal = nx_palettes[s_theme];
    nx_inval_all();
}

uint8_t nx_render_theme_get(void)
{
    return s_theme;
}

const uint16_t *nx_render_pal(void)
{
    return s_pal;
}

#if NX_ENG_VGRAD
void nx_render_bg(uint8_t tok_top, uint8_t tok_bottom)
{
    s_probe = -1;
    s_bg_top = tok_top;
    s_bg_bot = tok_bottom;
}
#endif

void nx_render_scrim(uint8_t level32)
{
    s_probe = -1;
    s_scrim = level32 > 32u ? 32u : level32;
}

void nx_render_layer(uint8_t layer, nx_draw_fn fn, void *ctx)
{
    s_probe = -1;
    if (layer < NX_LAYER_COUNT) {
        s_layer[layer].fn = fn;
        s_layer[layer].ctx = ctx;
    }
}

void nx_render_layer_clip(uint8_t layer, const nx_rect *clip)
{
    s_probe = -1;
    if (layer < NX_LAYER_COUNT)
        s_layer[layer].clip = clip ? *clip : k_screen;
}

void nx_render_layer_offset(uint8_t layer, int16_t dx, int16_t dy)
{
    s_probe = -1;
    if (layer < NX_LAYER_COUNT) {
        s_layer[layer].dx = dx;
        s_layer[layer].dy = dy;
    }
}

#if NX_ENG_FADE
void nx_render_layer_fade(uint8_t layer, uint8_t level32)
{
    s_probe = -1;
    if (layer < NX_LAYER_COUNT)
        s_layer[layer].fade = level32 > 32u ? 32u : level32;
}
#endif

void nx_render_on_frame(void (*fn)(void))
{
    s_on_frame = fn;
}

/* ---- dirty rectangles ---- */

int nx_rect_mergeable(const nx_rect *a, const nx_rect *b)
{
    nx_rect i = nx_rect_and(*a, *b), u = nx_rect_or(*a, *b);
    if (!nx_rect_empty(&i))
        return 1;
    /* the union costs at most 30 % more pixels than both apart */
    return nx_rect_area(&u) * 10 <= (nx_rect_area(a) + nx_rect_area(b)) * 13;
}

static void remove_at(unsigned i)
{
    s_dirty[i] = s_dirty[--s_ndirty];
}

static void add(nx_rect r)
{
    for (;;) {
        unsigned i;
        for (i = 0; i < s_ndirty; i++)
            if (nx_rect_mergeable(&s_dirty[i], &r))
                break;
        if (i < s_ndirty) {
            r = nx_rect_or(s_dirty[i], r);
            remove_at(i);
            continue;
        }
        if (s_ndirty < NX_DIRTY_MAX) {
            s_dirty[s_ndirty++] = r;
            return;
        }
        /* full: merge the pair (r included) whose union grows the least */
        int32_t best = INT32_MAX;
        unsigned bi = 0, bj = NX_DIRTY_MAX;
        for (i = 0; i < s_ndirty; i++) {
            nx_rect u = nx_rect_or(s_dirty[i], r);
            int32_t g = nx_rect_area(&u) - nx_rect_area(&s_dirty[i]) - nx_rect_area(&r);
            if (g < best) {
                best = g;
                bi = i;
                bj = NX_DIRTY_MAX;
            }
            for (unsigned j = i + 1; j < s_ndirty; j++) {
                u = nx_rect_or(s_dirty[i], s_dirty[j]);
                g = nx_rect_area(&u) - nx_rect_area(&s_dirty[i]) - nx_rect_area(&s_dirty[j]);
                if (g < best) {
                    best = g;
                    bi = i;
                    bj = j;
                }
            }
        }
        if (bj == NX_DIRTY_MAX) {
            r = nx_rect_or(s_dirty[bi], r);
            remove_at(bi);
            continue;
        }
        nx_rect m = nx_rect_or(s_dirty[bi], s_dirty[bj]);
        remove_at(bj);   /* bj > bi: removing it first keeps bi valid */
        remove_at(bi);
        add(m);          /* 14 left: this cannot recurse again */
    }
}

void nx_inval(nx_rect r)
{
    r = nx_rect_and(r, k_screen);
    if (!nx_rect_empty(&r))
        add(r);
}

void nx_inval_all(void)
{
    s_dirty[0] = k_screen;
    s_ndirty = 1;
}

void nx_inval_text(const nx_font_t *f, int16_t x, int16_t y, const char *old, const char *now)
{
    int16_t co[TEXT_CELLS + 1], cn[TEXT_CELLS + 1];
    unsigned no = nx_text_cells(f, old, x, co, TEXT_CELLS), nn = nx_text_cells(f, now, x, cn, TEXT_CELLS);
    if (no != nn) {
        int16_t end = co[no] > cn[nn] ? co[no] : cn[nn];
        nx_inval((nx_rect){ x, y, end, (int16_t)(y + f->height) });
        return;
    }
    for (unsigned i = 0; i < nn; i++)
        if (old[i] != now[i])
            nx_inval((nx_rect){ cn[i], y, cn[i + 1], (int16_t)(y + f->height) });
}

unsigned nx_dirty_count(void)
{
    return s_ndirty;
}

nx_rect nx_dirty_get(unsigned i)
{
    return i < s_ndirty ? s_dirty[i] : (nx_rect){ 0, 0, 0, 0 };
}

_Static_assert(NX_BAND_PX >= NX_LCD_W && NX_BAND_PX <= 65535u,
               "a band holds at least one full-width row and fits one DMA transfer");

int16_t nx_band_rows(int16_t w, int16_t h)
{
    if (w <= 0)
        return h;
    int16_t rows = (int16_t)(NX_BAND_PX / (unsigned)w);
    return rows < h ? rows : h;
}

/* ---- composition ---- */

static uint8_t tint_of(unsigned layer)
{
    unsigned base = layer == NX_LAYER_SCREEN ? s_scrim : 0u;
    if (!NX_ENG_FADE)
        return (uint8_t)base;
    unsigned fade = 32u - s_layer[layer].fade;   /* how far towards bg */
    return (uint8_t)(32u - ((32u - base) * (32u - fade) >> 5));
}

static void draw_layers(nx_gc *gc)
{
    nx_rect all = gc->clip;
    for (unsigned l = 0; l < NX_LAYER_COUNT; l++) {
        const struct layer *L = &s_layer[l];
        if (!L->fn)
            continue;
        gc->clip = nx_rect_and(all, L->clip);
        if (nx_rect_empty(&gc->clip))
            continue;
        gc->ox = L->dx;
        gc->oy = L->dy;
        gc->tint = tint_of(l);
        L->fn(gc, L->ctx);
    }
    gc->clip = all;
    gc->ox = gc->oy = 0;
}

static uint16_t bg_color(void)
{
    return s_scrim ? nx_mix(s_pal[BG_TOP], s_pal[NX_PAL_BG], s_scrim) : s_pal[BG_TOP];
}

/* 1 when only the (solid) background shows in r: it can go out as a fill. The answer for the last
 * band is kept: a band waiting for a free buffer is not probed again on every slice. */
static int plain(const nx_rect *r)
{
#if NX_ENG_VGRAD
    if (s_bg_top != s_bg_bot)
        return 0;
#endif
    if (s_probe >= 0 && r->x0 == s_probed.x0 && r->y0 == s_probed.y0 && r->x1 == s_probed.x1 && r->y1 == s_probed.y1)
        return s_probe;
    nx_gc gc = { 0, *r, *r, s_pal, 0, 0, 0, 0 };
    draw_layers(&gc);
    s_probed = *r;
    s_probe = (int8_t)(gc.hits == 0);
    return s_probe;
}

int nx_render_band(const nx_rect *band)
{
    if (!nx_lcd_room())
        return 0;
    if (plain(band)) {
        if (!nx_lcd_fill(band, bg_color()))
            return 0;
        g_nx_render_stats.fills++;
        return 1;
    }
    uint16_t *buf = nx_lcd_buf_get();
    if (!buf)
        return 0;
    uint16_t t0 = nx_us16();
    nx_gc gc = { buf, *band, *band, s_pal, 0, 0, s_scrim, 0 };
#if NX_ENG_VGRAD
    if (s_bg_top != s_bg_bot)
        nx_vgrad(&gc, k_screen, nx_col(&gc, s_bg_top), nx_col(&gc, s_bg_bot));
    else
#endif
        nx_fill(&gc, *band, bg_color());
    draw_layers(&gc);
    uint16_t dt = (uint16_t)(nx_us16() - t0);
    s_band_us = dt;
    g_nx_render_stats.draw_us += dt;
    if (dt > g_nx_render_stats.band_us_max)
        g_nx_render_stats.band_us_max = dt;
    if (!nx_lcd_band(buf, band)) {
        nx_lcd_buf_put(buf);
        return 0;
    }
    g_nx_render_stats.bands++;
    return 1;
}

/* ---- frames ---- */

static void frame_start(void)
{
    /* this frame's rectangles, top to bottom (insertion sort, at most 16) */
    s_ncur = 0;
    for (unsigned i = 0; i < s_ndirty; i++) {
        nx_rect r = s_dirty[i];
        unsigned j = s_ncur++;
        while (j > 0 && (s_cur[j - 1].y0 > r.y0 || (s_cur[j - 1].y0 == r.y0 && s_cur[j - 1].x0 > r.x0))) {
            s_cur[j] = s_cur[j - 1];
            j--;
        }
        s_cur[j] = r;
    }
    s_ndirty = 0;
    s_icur = 0;
    s_in_frame = 1;
    s_probe = -1;
    s_frame_t0 = nx_ms();
    if (s_on_frame)
        s_on_frame();
}

void nx_render_restart(void)
{
    s_probe = -1;
    s_ndirty = s_ncur = s_icur = 0;
    s_in_rect = 0;
    s_in_frame = 0;
    if (s_on_frame)
        s_on_frame();
}

int nx_render_pending(void)
{
    return s_ndirty || s_icur < s_ncur || s_in_rect || !nx_lcd_idle();
}

int nx_render_step(uint32_t budget_us)
{
    uint16_t t0 = nx_us16();
    uint32_t now = nx_ms();
    int more = 1;
    unsigned drawn = 0;
    nx_lcd_poll();
    nx_lcd_power_step(now);
    if (!nx_lcd_ready())
        return 0;               /* asleep or waking up: the dirty list waits */
    if (nx_trans_step(now))
        goto out;               /* the transition owns the bus this slice */
    for (;;) {
        if (!s_in_rect) {
            if (s_icur >= s_ncur) {
                if (s_in_frame && nx_lcd_idle()) {
                    s_in_frame = 0;
                    g_nx_render_stats.frames++;
                    g_nx_render_stats.frame_ms_last = now - s_frame_t0;
                }
                if (!s_ndirty) {
                    more = !nx_lcd_idle();
                    goto out;
                }
                frame_start();
            }
            s_rect = s_cur[s_icur];
            if (plain(&s_rect)) {   /* nothing but background in the whole rectangle */
                if (!nx_lcd_fill(&s_rect, bg_color()))
                    goto out;
                g_nx_render_stats.fills++;
                s_icur++;
                continue;
            }
            s_icur++;
            s_in_rect = 1;
            s_y = s_rect.y0;
            s_bh = nx_band_rows((int16_t)(s_rect.x1 - s_rect.x0), (int16_t)(s_rect.y1 - s_rect.y0));
        }
        /* after the first band, another one is started only if it can finish within the budget
         * (assuming it takes as long as the last one) */
        uint16_t used = (uint16_t)(nx_us16() - t0);
        if (drawn && used + s_band_us > budget_us)
            goto out;
        nx_rect band = { s_rect.x0, s_y, s_rect.x1, (int16_t)(s_y + s_bh < s_rect.y1 ? s_y + s_bh : s_rect.y1) };
        if (!nx_render_band(&band))
            goto out;           /* both buffers busy: the DMA interrupt frees one */
        drawn++;
        s_y = band.y1;
        if (s_y >= s_rect.y1)
            s_in_rect = 0;
    }
out:;
    uint16_t dt = (uint16_t)(nx_us16() - t0);
    if (dt > g_nx_render_stats.step_us_max)
        g_nx_render_stats.step_us_max = dt;
    return more;
}

int nx_render_busy(void)
{
    return nx_lcd_ready() && (nx_render_pending() || nx_trans_active());
}

int nx_render_flush(void)
{
    uint32_t spent = 0;
    uint16_t last = nx_us16();
    for (uint32_t n = 0; n < FLUSH_SPINS && spent < FLUSH_MAX_US; n++) {
        nx_render_step(10000u);
        if (!nx_render_busy() && nx_lcd_idle())
            return 1;
        uint16_t now = nx_us16();
        spent += (uint16_t)(now - last);
        last = now;
    }
    return 0;
}

/* ---- classic lcd.h for the shared factory port ----
 * factory.c clears the screen with lcd_fill_rect(0, 0, 239, 319, black) around its calibration
 * screen (':a0' / ':a1'). In nx the UI owns every pixel, so the call marks the area for
 * repainting from the UI's own state; the colour does not matter. Physical to logical:
 * X = 319 - y, Y = x (lcd.h). */
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    (void)color;
    nx_inval((nx_rect){ (int16_t)(LCD_H - 1u - y1), (int16_t)x0, (int16_t)(LCD_H - y0), (int16_t)(x1 + 1u) });
}
