/* nx raster core (nx_gfx.h). The Cortex-M0+ has a single-cycle multiplier but no divider and no
 * FPU: coverage comes from squared distances (no square roots), divisions happen once per
 * primitive, never per pixel. */
#include "nx_gfx.h"

#include "nx_font.h"

typedef uint32_t __attribute__((may_alias)) u32_alias;

const uint8_t k_nx_a16[16] = { 0, 2, 4, 6, 9, 11, 13, 15, 17, 19, 21, 23, 26, 28, 30, 32 };

#if NX_ENG_VGRAD
/* 4x4 ordered-dither thresholds, 0..15 */
static const uint8_t k_bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
#endif

/* sin of i/64 of a quarter turn, Q14 */
static const uint16_t k_qsin[65] = {
    0,     402,   804,   1205,  1606,  2006,  2404,  2801,  3196,  3590,  3981,  4370,  4756,
    5139,  5520,  5897,  6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,  9102,  9434,
    9760,  10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557,
    15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384,
};

/* ---- rectangles ---- */

int nx_rect_empty(const nx_rect *r)
{
    return r->x0 >= r->x1 || r->y0 >= r->y1;
}

nx_rect nx_rect_and(nx_rect a, nx_rect b)
{
    nx_rect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1,
                  a.y1 < b.y1 ? a.y1 : b.y1 };
    return r;
}

nx_rect nx_rect_or(nx_rect a, nx_rect b)
{
    if (nx_rect_empty(&a))
        return b;
    if (nx_rect_empty(&b))
        return a;
    nx_rect r = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                  a.y1 > b.y1 ? a.y1 : b.y1 };
    return r;
}

int32_t nx_rect_area(const nx_rect *r)
{
    return nx_rect_empty(r) ? 0 : (int32_t)(r->x1 - r->x0) * (int32_t)(r->y1 - r->y0);
}

/* ---- colour ---- */

/* Spread form 00000GGGGGG00000RRRRR000000BBBBB: every channel has room above it for the
 * product, so one multiply blends all three (exactly bg + floor((fg - bg) * a / 32)). */
static inline uint16_t blend(uint16_t bg, uint16_t fg, unsigned a)
{
    uint32_t b = (bg | (uint32_t)bg << 16) & 0x07E0F81Fu;
    uint32_t f = (fg | (uint32_t)fg << 16) & 0x07E0F81Fu;
    uint32_t r = (b + (((f - b) * a) >> 5)) & 0x07E0F81Fu;
    return (uint16_t)(r | r >> 16);
}

uint16_t nx_mix(uint16_t bg, uint16_t fg, unsigned a)
{
    return blend(bg, fg, a);
}

uint16_t nx_col(const nx_gc *gc, uint8_t tok)
{
    uint16_t c = gc->pal[tok];
    return gc->tint ? nx_mix(c, gc->pal[NX_PAL_BG], gc->tint) : c;
}

nx_rect nx_clip_push(nx_gc *gc, nx_rect r)
{
    nx_rect prev = gc->clip;
    r.x0 = (int16_t)(r.x0 + gc->ox);
    r.x1 = (int16_t)(r.x1 + gc->ox);
    r.y0 = (int16_t)(r.y0 + gc->oy);
    r.y1 = (int16_t)(r.y1 + gc->oy);
    gc->clip = nx_rect_and(gc->clip, r);
    return prev;
}

void nx_clip_pop(nx_gc *gc, nx_rect prev)
{
    gc->clip = prev;
}

/* ---- pixel access ---- */

static inline uint16_t *pix(const nx_gc *gc, int x, int y)
{
    return gc->px + (y - gc->band.y0) * (gc->band.x1 - gc->band.x0) + (x - gc->band.x0);
}

static inline nx_rect moved(const nx_gc *gc, nx_rect r)
{
    r.x0 = (int16_t)(r.x0 + gc->ox);
    r.x1 = (int16_t)(r.x1 + gc->ox);
    r.y0 = (int16_t)(r.y0 + gc->oy);
    r.y1 = (int16_t)(r.y1 + gc->oy);
    return r;
}

/* Clips r (screen coordinates) in place; counts the hit; 1 when there is something to draw. */
static int take(nx_gc *gc, nx_rect *r)
{
    *r = nx_rect_and(*r, gc->clip);
    if (nx_rect_empty(r))
        return 0;
    gc->hits++;
    return gc->px != 0;
}

static inline void put(uint16_t *p, uint16_t c, unsigned a)
{
    if (a >= 32u)
        *p = c;
    else if (a)
        *p = blend(*p, c, a);
}

static void span(uint16_t *p, int n, uint16_t c)
{
    if (n <= 0)
        return;
    if ((uintptr_t)p & 2u) {
        *p++ = c;
        n--;
    }
    u32_alias *q = (u32_alias *)p;
    uint32_t cc = c | (uint32_t)c << 16;
    for (; n >= 2; n -= 2)
        *q++ = cc;
    if (n)
        *(uint16_t *)q = c;
}

/* ---- fills ---- */

void nx_fill(nx_gc *gc, nx_rect r, uint16_t c)
{
    r = moved(gc, r);
    if (!take(gc, &r))
        return;
    for (int y = r.y0; y < r.y1; y++)
        span(pix(gc, r.x0, y), r.x1 - r.x0, c);
}

#if NX_VENDOR_SPLASH
void nx_mask1(nx_gc *gc, nx_rect r, const uint8_t *bits, uint16_t c)
{
    nx_rect full = moved(gc, r), v = full;
    if (!take(gc, &v))
        return;
    unsigned stride = (unsigned)(full.x1 - full.x0 + 7) / 8u;
    for (int y = v.y0; y < v.y1; y++) {
        const uint8_t *row = bits + (unsigned)(y - full.y0) * stride;
        uint16_t *p = pix(gc, v.x0, y);
        for (int x = v.x0; x < v.x1; x++, p++) {
            unsigned i = (unsigned)(x - full.x0);
            if (row[i >> 3] & (0x80u >> (i & 7u)))
                *p = c;
        }
    }
}
#endif

#if NX_ENG_VGRAD
void nx_vgrad(nx_gc *gc, nx_rect r, uint16_t top, uint16_t bottom)
{
    nx_rect full = moved(gc, r), v = full;
    if (!take(gc, &v))
        return;
    int32_t h = full.y1 - full.y0;
    int32_t r0 = top >> 11, g0 = (top >> 5) & 63, b0 = top & 31;
    int32_t dr = (bottom >> 11) - r0, dg = ((bottom >> 5) & 63) - g0, db = (bottom & 31) - b0;
    for (int y = v.y0; y < v.y1; y++) {
        int32_t t = h > 1 ? (int32_t)(y - full.y0) * 256 / (h - 1) : 0;   /* 0..256 */
        int32_t rq = r0 * 16 + ((dr * 16 * t) >> 8);                        /* channel in Q4 */
        int32_t gq = g0 * 16 + ((dg * 16 * t) >> 8);
        int32_t bq = b0 * 16 + ((db * 16 * t) >> 8);
        const uint8_t *th = k_bayer[y & 3];
        uint16_t *p = pix(gc, v.x0, y);
        for (int x = v.x0; x < v.x1; x++) {
            int32_t d = th[x & 3];
            *p++ = (uint16_t)(((rq + d) >> 4) << 11 | ((gq + d) >> 4) << 5 | ((bq + d) >> 4));
        }
    }
}
#endif

/* ---- circles, rounded rectangles, pills ---- */

/* Coverage 0..32 of the pixel whose centre lies (dx, dy) from the centre of a circle of radius
 * rq, all in Q4: about r - d + 1/2 px, as (r^2 - d^2) / 2r (no square root). inv = 65536 / rq. */
static unsigned cov_circle(int32_t dx, int32_t dy, int32_t rq, uint32_t inv)
{
    int32_t num = rq * rq - (dx * dx + dy * dy) + 16 * rq;
    if (num <= 0)
        return 0;
    if (num >= 32 * rq)
        return 32;
    return (unsigned)(((uint32_t)num * inv) >> 16);
}

static unsigned cov_rrect(int x, int y, const nx_rect *r, int rad, uint32_t inv)
{
    if (x < r->x0 || x >= r->x1 || y < r->y0 || y >= r->y1)
        return 0;
    int cx, cy;
    if (x < r->x0 + rad)
        cx = r->x0 + rad;
    else if (x >= r->x1 - rad)
        cx = r->x1 - rad;
    else
        return 32;
    if (y < r->y0 + rad)
        cy = r->y0 + rad;
    else if (y >= r->y1 - rad)
        cy = r->y1 - rad;
    else
        return 32;
    return cov_circle(x * 16 + 8 - cx * 16, y * 16 + 8 - cy * 16, rad * 16, inv);
}

static int clamp_rad(const nx_rect *r, int rad)
{
    int h = (r->y1 - r->y0) / 2, w = (r->x1 - r->x0) / 2;
    if (rad > h)
        rad = h;
    if (rad > w)
        rad = w;
    return rad < 0 ? 0 : rad;
}

void nx_rrect(nx_gc *gc, nx_rect r, uint8_t rad_in, uint16_t c)
{
    r = moved(gc, r);
    nx_rect v = r;
    if (!take(gc, &v))
        return;
    int rad = clamp_rad(&r, rad_in);
    uint32_t inv = rad ? 65536u / (uint32_t)(rad * 16) : 0;
    int xa = r.x0 + rad, xb = r.x1 - rad;   /* columns outside the corners */
    for (int y = v.y0; y < v.y1; y++) {
        if (y >= r.y0 + rad && y < r.y1 - rad) {
            span(pix(gc, v.x0, y), v.x1 - v.x0, c);
            continue;
        }
        uint16_t *p = pix(gc, v.x0, y);
        for (int x = v.x0; x < v.x1; x++, p++) {
            if (x >= xa && x < xb) {
                int n = (xb < v.x1 ? xb : v.x1) - x;
                span(p, n, c);
                p += n - 1;
                x += n - 1;
                continue;
            }
            put(p, c, cov_rrect(x, y, &r, rad, inv));
        }
    }
}

void nx_rrect_line(nx_gc *gc, nx_rect r, uint8_t rad_in, uint8_t w, uint16_t c)
{
    r = moved(gc, r);
    nx_rect v = r;
    if (!take(gc, &v))
        return;
    int rad = clamp_rad(&r, rad_in);
    nx_rect in = { (int16_t)(r.x0 + w), (int16_t)(r.y0 + w), (int16_t)(r.x1 - w), (int16_t)(r.y1 - w) };
    int irad = rad > w ? rad - w : 0;
    uint32_t inv = rad ? 65536u / (uint32_t)(rad * 16) : 0;
    uint32_t iinv = irad ? 65536u / (uint32_t)(irad * 16) : 0;
    int edge = rad > w ? rad : w;   /* rows that need the per-pixel test */
    for (int y = v.y0; y < v.y1; y++) {
        if (y >= r.y0 + edge && y < r.y1 - edge) {
            nx_rect ls = { r.x0, 0, (int16_t)(r.x0 + w), 0 }, rs = { (int16_t)(r.x1 - w), 0, r.x1, 0 };
            int a0 = ls.x0 > v.x0 ? ls.x0 : v.x0, a1 = ls.x1 < v.x1 ? ls.x1 : v.x1;
            int b0 = rs.x0 > v.x0 ? rs.x0 : v.x0, b1 = rs.x1 < v.x1 ? rs.x1 : v.x1;
            if (a1 > a0)
                span(pix(gc, a0, y), a1 - a0, c);
            if (b1 > b0)
                span(pix(gc, b0, y), b1 - b0, c);
            continue;
        }
        uint16_t *p = pix(gc, v.x0, y);
        for (int x = v.x0; x < v.x1; x++, p++) {
            unsigned a = cov_rrect(x, y, &r, rad, inv);
            if (a && !nx_rect_empty(&in)) {
                unsigned ai = cov_rrect(x, y, &in, irad, iinv);
                a = a > ai ? a - ai : 0;
            }
            put(p, c, a);
        }
    }
}

/* Pill between x = xs and xe (Q4) over the rows of r, round caps of radius h/2. */
static void pill_q4(nx_gc *gc, const nx_rect *r, int32_t xs, int32_t xe, uint16_t c)
{
    nx_rect b = { (int16_t)(xs >> 4), r->y0, (int16_t)((xe + 15) >> 4), r->y1 };
    if (!take(gc, &b))
        return;
    int32_t rq = (r->y1 - r->y0) * 8;   /* h / 2 in Q4 */
    uint32_t inv = rq ? 65536u / (uint32_t)rq : 0;
    int32_t cy = (r->y0 + r->y1) * 8, lc = xs + rq, rc = xe - rq;
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *p = pix(gc, b.x0, y);
        int32_t dy = y * 16 + 8 - cy;
        for (int x = b.x0; x < b.x1; x++, p++) {
            int32_t px = x * 16 + 8;
            unsigned a = 32, t;
            if (px < lc) {
                t = cov_circle(px - lc, dy, rq, inv);
                a = t < a ? t : a;
            }
            if (px > rc) {
                t = cov_circle(px - rc, dy, rq, inv);
                a = t < a ? t : a;
            }
            put(p, c, a);
        }
    }
}

void nx_pill(nx_gc *gc, nx_rect r, uint16_t c)
{
    r = moved(gc, r);
    pill_q4(gc, &r, r.x0 * 16, r.x1 * 16, c);
}

void nx_bar(nx_gc *gc, nx_rect r, uint16_t frac, uint16_t fill, uint16_t track)
{
    r = moved(gc, r);
    int32_t w = r.x1 - r.x0;
    pill_q4(gc, &r, r.x0 * 16, r.x1 * 16, track);
    int32_t len = (int32_t)((uint32_t)w * 16u * frac / 65535u);   /* fill length, Q4 */
    if (len > 0)
        pill_q4(gc, &r, r.x0 * 16, r.x0 * 16 + len, fill);
}

void nx_segbar_layout(int16_t w, const uint16_t *dur, uint8_t n, int16_t *x0, int16_t *x1)
{
    if (!n)
        return;
    int32_t avail = w - NX_SEG_GAP * (n - 1);
    int32_t extra = avail - NX_SEG_MIN_W * n;   /* shared in proportion to the durations */
    uint32_t sum = 0, acc = 0;
    for (unsigned i = 0; i < n; i++)
        sum += dur[i];
    int32_t x = 0, prev = 0;
    for (unsigned i = 0; i < n; i++) {
        int32_t sw;
        acc += dur[i];
        if (extra > 0) {
            int32_t t = sum ? (int32_t)((uint32_t)extra * acc / sum) : extra * (int32_t)(i + 1) / n;
            sw = NX_SEG_MIN_W + t - prev;
            prev = t;
        } else {   /* too narrow for the minimum: equal widths */
            int32_t t = avail * (int32_t)(i + 1) / n;
            sw = t - prev;
            prev = t;
        }
        x0[i] = (int16_t)x;
        x1[i] = (int16_t)(x + sw);
        x += sw + NX_SEG_GAP;
    }
}

void nx_segbar(nx_gc *gc, nx_rect r, const uint16_t *dur, uint8_t n, uint8_t cur, uint16_t cur_frac,
               uint16_t c_done, uint16_t c_cur, uint16_t c_todo)
{
    nx_rect bb = moved(gc, r);
    if (!take(gc, &bb) || n == 0)
        return;
    int16_t x0[NX_SEG_MAX], x1[NX_SEG_MAX];
    if (n > NX_SEG_MAX)
        n = NX_SEG_MAX;
    nx_segbar_layout((int16_t)(r.x1 - r.x0), dur, n, x0, x1);
    for (unsigned i = 0; i < n; i++) {
        nx_rect s = { (int16_t)(r.x0 + x0[i]), r.y0, (int16_t)(r.x0 + x1[i]), r.y1 };
        if (i < cur)
            nx_pill(gc, s, c_done);
        else if (i == cur)
            nx_bar(gc, s, cur_frac, c_cur, c_todo);
        else
            nx_pill(gc, s, c_todo);
    }
}

/* ---- angles, rings, discs ---- */

int32_t nx_sin1024(uint32_t a)
{
    a &= 1023u;
    uint32_t q = a >> 8, f = a & 255u;
    if (q & 1u)
        f = 256u - f;
    uint32_t i = f >> 2, fr = f & 3u;
    int32_t v = k_qsin[i];
    if (fr)
        v += (((int32_t)k_qsin[i + 1] - v) * (int32_t)fr) >> 2;
    return (q & 2u) ? -v : v;
}

int32_t nx_cos1024(uint32_t a)
{
    return nx_sin1024(a + 256u);
}

static inline unsigned clamp32(int32_t v)
{
    return v <= 0 ? 0u : v >= 32 ? 32u : (unsigned)v;
}

void nx_ring(nx_gc *gc, int16_t cx, int16_t cy, uint8_t r, uint8_t w, uint16_t a0, uint16_t a1, int caps,
             uint16_t c)
{
    cx = (int16_t)(cx + gc->ox);
    cy = (int16_t)(cy + gc->oy);
    nx_rect bb = { (int16_t)(cx - r), (int16_t)(cy - r), (int16_t)(cx + r), (int16_t)(cy + r) };
    if (!take(gc, &bb) || !r)
        return;
    if (w > r)
        w = r;
    int32_t ro = r * 16, ri = (r - w) * 16;
    uint32_t invo = 65536u / (uint32_t)ro, invi = ri ? 65536u / (uint32_t)ri : 0;
    int32_t sweep = (int32_t)a1 - (int32_t)a0;   /* a1 >= a0; 1024 or more: the whole ring */
    int full = sweep >= 1024, wide = sweep > 512;
    int32_t v0x = nx_sin1024(a0), v0y = -nx_cos1024(a0), v1x = nx_sin1024(a1), v1y = -nx_cos1024(a1);
    int32_t rm = (ro + ri) / 2, rc = (ro - ri) / 2;
    uint32_t invc = rc ? 65536u / (uint32_t)rc : 0;
    int32_t c0x = (rm * v0x) >> 14, c0y = (rm * v0y) >> 14, c1x = (rm * v1x) >> 14, c1y = (rm * v1y) >> 14;
    int use_caps = caps && !full && rc > 0;
    for (int y = bb.y0; y < bb.y1; y++) {
        uint16_t *p = pix(gc, bb.x0, y);
        int32_t dy = y * 16 + 8 - cy * 16;
        for (int x = bb.x0; x < bb.x1; x++, p++) {
            int32_t dx = x * 16 + 8 - cx * 16;
            unsigned a = cov_circle(dx, dy, ro, invo);
            if (a && ri) {
                unsigned ai = cov_circle(dx, dy, ri, invi);
                a = a > ai ? a - ai : 0;
            }
            if (a && !full) {
                unsigned e0 = clamp32(16 + ((v0x * dy - v0y * dx) >> 13));
                unsigned e1 = clamp32(16 + ((dx * v1y - dy * v1x) >> 13));
                unsigned ang = wide ? (e0 > e1 ? e0 : e1) : (e0 < e1 ? e0 : e1);
                a = (a * ang) >> 5;
            }
            if (use_caps) {
                unsigned t = cov_circle(dx - c0x, dy - c0y, rc, invc);
                if (t > a)
                    a = t;
                t = cov_circle(dx - c1x, dy - c1y, rc, invc);
                if (t > a)
                    a = t;
            }
            put(p, c, a);
        }
    }
}

void nx_disc(nx_gc *gc, int32_t cx, int32_t cy, int32_t rq, uint16_t c)
{
    cx += gc->ox * 16;
    cy += gc->oy * 16;
    nx_rect bb = { (int16_t)((cx - rq) >> 4), (int16_t)((cy - rq) >> 4), (int16_t)((cx + rq + 15) >> 4),
                   (int16_t)((cy + rq + 15) >> 4) };
    if (!take(gc, &bb) || rq <= 0)
        return;
    uint32_t inv = 65536u / (uint32_t)rq;
    for (int y = bb.y0; y < bb.y1; y++) {
        uint16_t *p = pix(gc, bb.x0, y);
        for (int x = bb.x0; x < bb.x1; x++, p++)
            put(p, c, cov_circle(x * 16 + 8 - cx, y * 16 + 8 - cy, rq, inv));
    }
}

/* ---- polyline ---- */

void nx_trace(nx_gc *gc, int16_t x0, int16_t n, nx_trace_fn fy, void *ctx, int16_t hw, uint16_t c, unsigned a32)
{
    x0 = (int16_t)(x0 + gc->ox);
    nx_rect bb = { x0, gc->clip.y0, (int16_t)(x0 + n), gc->clip.y1 };
    if (!take(gc, &bb))
        return;
    int32_t oy = gc->oy * 16;
    int32_t prev = fy(ctx, -1) + oy, cur = fy(ctx, 0) + oy;
    for (int16_t i = 0; i < n; i++) {
        int32_t next = fy(ctx, (int16_t)(i + 1)) + oy;
        int x = x0 + i;
        if (x >= bb.x0 && x < bb.x1) {
            int32_t m0 = (cur + prev) / 2, m1 = (cur + next) / 2;
            int32_t lo = cur, hi = cur;
            if (m0 < lo)
                lo = m0;
            if (m1 < lo)
                lo = m1;
            if (m0 > hi)
                hi = m0;
            if (m1 > hi)
                hi = m1;
            lo -= hw;
            hi += hw;
            int ya = lo >> 4, yb = (hi + 15) >> 4;
            if (ya < bb.y0)
                ya = bb.y0;
            if (yb > bb.y1)
                yb = bb.y1;
            for (int y = ya; y < yb; y++) {
                int32_t top = lo > y * 16 ? lo : y * 16, bot = hi < y * 16 + 16 ? hi : y * 16 + 16;
                if (bot > top)
                    put(pix(gc, x, y), c, (unsigned)((bot - top) * (int32_t)a32) >> 4);
            }
        }
        prev = cur;
        cur = next;
    }
}

/* ---- glyphs and text ---- */

static inline unsigned nib(const uint8_t *d, uint32_t k)
{
    return (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
}

/* Glyph (or icon) g of f with its bitmap's top-left at (gx, gy), straight from the table into the
 * band (format: nx_asset_fmt.h). RLE runs are handled as runs: transparent ones skipped, opaque
 * ones filled, so a glyph costs little more than its visible edge pixels. Rows above the band are
 * walked, not drawn; the walk stops below it. */
static void blit_mask(nx_gc *gc, const nx_font_t *f, const nx_glyph_t *g, int gx, int gy, uint16_t c)
{
    nx_rect r = { (int16_t)gx, (int16_t)gy, (int16_t)(gx + g->w), (int16_t)(gy + g->h) };
    if (!g->w || !take(gc, &r))
        return;
    const uint8_t *d = f->data + (g->off & NX_GLYPH_OFF_MASK);
    int w = g->w, ry0 = r.y0 - gy, ry1 = r.y1 - gy, cx0 = r.x0 - gx, cx1 = r.x1 - gx;
    if (!(g->off & NX_GLYPH_RLE)) {
        unsigned per = f->bpp == 4 ? 2u : 4u;   /* pixels per byte */
        for (int row = ry0; row < ry1; row++) {
            uint32_t p = (uint32_t)(row * w + cx0);
            uint16_t *dst = pix(gc, r.x0, gy + row);
            for (int x = cx0; x < cx1; x++, p++, dst++) {
                uint8_t b = d[per == 2u ? p >> 1 : p >> 2];
                if (!b && (p & (per - 1u)) == 0 && x + (int)per <= cx1) {   /* a transparent byte */
                    x += (int)per - 1;
                    p += per - 1;
                    dst += per - 1;
                    continue;
                }
                unsigned a = per == 2u ? ((p & 1u) ? b & 15u : b >> 4) : ((b >> (6u - 2u * (p & 3u))) & 3u) * 5u;
                if (a)
                    put(dst, c, k_nx_a16[a]);
            }
        }
        return;
    }
    uint32_t k = 0;
    int row = 0, col = 0;
    while (row < ry1) {
        unsigned a = nib(d, k++);
        int run = 1;
        if (a == 0 || a == NX_ALPHA_MAX)
            run = (int)nib(d, k++) + 1;
        if (row < ry0) {                      /* the part above the band is walked, not drawn */
            int above = (ry0 - row) * w - col;
            int skip = run < above ? run : above;
            col += skip;
            run -= skip;
            while (col >= w) {
                col -= w;
                row++;
            }
        }
        if (!a) {                             /* transparent: just move on */
            col += run;
            while (col >= w) {
                col -= w;
                row++;
            }
            continue;
        }
        while (run) {                         /* the run may continue on the next rows */
            int seg = w - col < run ? w - col : run;
            if (row >= ry0) {
                int x0 = col > cx0 ? col : cx0, x1 = col + seg < cx1 ? col + seg : cx1;
                if (x1 > x0) {
                    uint16_t *dst = pix(gc, gx + x0, gy + row);
                    if (a == NX_ALPHA_MAX) {
                        span(dst, x1 - x0, c);
                    } else {
                        for (int x = x0; x < x1; x++, dst++)
                            *dst = blend(*dst, c, k_nx_a16[a]);
                    }
                }
            }
            run -= seg;
            col += seg;
            if (col == w) {
                col = 0;
                if (++row >= ry1)
                    return;
            }
        }
    }
}

void nx_icon(nx_gc *gc, const nx_font_t *set, uint8_t id, int16_t x, int16_t y, uint16_t c)
{
    const nx_glyph_t *g = nx_font_glyph(set, id);
    if (g)
        blit_mask(gc, set, g, x + gc->ox + g->x_off, y + gc->oy + g->y_off, c);
}

int16_t nx_text_n(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, const char *s, size_t n, uint16_t c)
{
    int sy = y + gc->oy;
    if (sy >= gc->clip.y1 || sy + f->height <= gc->clip.y0)   /* whole line outside the band */
        return (int16_t)(x + nx_font_text_width_n(f, s, n));
    int px = x + gc->ox;
    for (size_t i = 0; i < n && s[i]; i++) {
        const nx_glyph_t *g = nx_font_glyph_or_fallback(f, (uint8_t)s[i]);
        if (!g)
            continue;
        if (px + g->x_off < gc->clip.x1 && px + g->x_off + g->w > gc->clip.x0)
            blit_mask(gc, f, g, px + g->x_off, sy + g->y_off, c);
        px += g->advance;
    }
    return (int16_t)(px - gc->ox);
}

int16_t nx_text(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, const char *s, uint16_t c)
{
    return nx_text_n(gc, f, x, y, s, (size_t)-1, c);
}

int16_t nx_text_fit(nx_gc *gc, const nx_font_t *f, int16_t x, int16_t y, int16_t max_w, const char *s, uint16_t c)
{
    nx_fit_t fit = nx_font_fit(f, s, max_w > 0 ? (unsigned)max_w : 0u);
    x = nx_text_n(gc, f, x, y, s, fit.n, c);
    if (fit.ellipsis)
        x = nx_text(gc, f, x, y, nx_font_ellipsis(f), c);
    return x;
}

void nx_text_marquee(nx_gc *gc, const nx_font_t *f, nx_rect box, const char *s, int16_t offset, uint16_t c)
{
    nx_rect prev = nx_clip_push(gc, box);
    int16_t w = (int16_t)nx_font_text_width(f, s);
    int16_t x = (int16_t)(box.x0 - offset);
    nx_text(gc, f, x, box.y0, s, c);
    if (offset > 0 && x + w + NX_MARQUEE_GAP < box.x1)
        nx_text(gc, f, (int16_t)(x + w + NX_MARQUEE_GAP), box.y0, s, c);
    nx_clip_pop(gc, prev);
}

unsigned nx_text_cells(const nx_font_t *f, const char *s, int16_t x, int16_t *cell_x, unsigned max)
{
    unsigned n = 0;
    for (; n < max && s[n]; n++) {
        cell_x[n] = x;
        const nx_glyph_t *g = nx_font_glyph_or_fallback(f, (uint8_t)s[n]);
        x = (int16_t)(x + (g ? g->advance : 0));
    }
    cell_x[n] = x;
    return n;
}

int16_t nx_marquee_offset(int16_t text_w, int16_t box_w, uint32_t t_ms)
{
    if (text_w <= box_w)
        return 0;
    uint32_t cycle = (uint32_t)text_w + NX_MARQUEE_GAP;        /* pixels until it repeats */
    uint32_t period = 1000u + cycle * 25u;                      /* 1 s still + 40 px/s */
    uint32_t t = t_ms % period;
    if (t < 1000u)
        return 0;
    uint32_t off = (t - 1000u) / 50u * 2u;                      /* 2 px steps at 20 Hz */
    return (int16_t)(off < cycle ? off : 0);
}
