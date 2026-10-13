/* Hero frequency (nx_hero.h). One 64-bit split per change (libgcc's division), then 32-bit digits. */
#include "nx_hero.h"

#include <string.h>

#include "nx_font.h"

#define NX_HZ_MAX 9999999999999999ull   /* 99999999.99999999 Hz in 1e-8 Hz */

static const uint64_t k_pow10[10] = { 1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
                                      10000000ull, 100000000ull, 1000000000ull };
/* largest m that m * 10^e does not exceed NX_HZ_MAX */
static const uint64_t k_mlim[10] = { 9999999999999999ull, 999999999999999ull, 99999999999999ull,
                                     9999999999999ull,    999999999999ull,    99999999999ull,
                                     9999999999ull,       999999999ull,       99999999ull,
                                     9999999ull };

void nx_hero_decode(uint64_t v, uint32_t *ip, uint32_t *frac, uint8_t *over)
{
    uint64_t m = v / 10u;
    unsigned e = (unsigned)(v - m * 10u);
    uint64_t n;
    *over = 0;
    if (m > k_mlim[e]) {   /* the classic firmware wrapped modulo 10^16 here */
        n = NX_HZ_MAX;
        *over = 1;
    } else {
        n = m * k_pow10[e];
    }
    uint64_t q = n / 100000000u;
    *ip = (uint32_t)q;
    *frac = (uint32_t)(n - q * 100000000u);
}

static const nx_font_t *big_font(uint8_t f)
{
#if NX_FEAT_HERO48
    if (f == NX_HERO_D1)
        return &nx_font_num48;
#else
    (void)f;
#endif
    return &nx_font_num28;
}

static int16_t adv(const nx_font_t *f, char c)
{
    const nx_glyph_t *g = nx_font_glyph(f, (uint8_t)c);
    return g ? g->advance : 0;
}

static int16_t width(const nx_font_t *f, const char *s, unsigned n, uint32_t gaps, int16_t gap)
{
    int16_t w = 0;
    for (unsigned i = 0; i < n; i++)
        w = (int16_t)(w + adv(f, s[i]) + ((gaps >> i) & 1u ? gap : 0));
    return w;
}

static void build(nx_hero *h, const char *id, unsigned ni, const char *fd, uint8_t font, uint8_t k)
{
    unsigned n = 0;
    h->font = font;
    h->k = k;
    h->big_gaps = h->tail_gaps = 0;
    for (unsigned i = 0; i < ni; i++) {
        if (i && (ni - i) % 3u == 0)
            h->big_gaps |= 1u << n;
        h->big[n++] = id[i];
    }
    if (k) {
        h->big[n++] = '.';
        for (unsigned j = 1; j <= k; j++) {   /* decimal position j: groups start at 4 and 7 */
            if (j == 4 || j == 7)
                h->big_gaps |= 1u << n;
            h->big[n++] = fd[j - 1];
        }
    }
    h->big[n] = 0;
    h->n_big = (uint8_t)n;
    n = 0;
    if (k == 0)
        h->tail[n++] = '.';
    for (unsigned j = k + 1u; j <= 8u; j++) {
        if ((j == 4 || j == 7) && j > k + 1u)
            h->tail_gaps |= 1u << n;
        h->tail[n++] = fd[j - 1];
    }
    h->tail[n] = 0;
    h->n_tail = (uint8_t)n;
    const nx_font_t *bf = big_font(font), *tf = &nx_font_body16;
    h->w_big = width(bf, h->big, h->n_big, h->big_gaps, NX_HERO_IS_D1(font) ? NX_HERO_GAP_D1 : NX_HERO_GAP_D2);
    h->w_tail = width(tf, h->tail, h->n_tail, h->tail_gaps, NX_HERO_GAP_TAIL);
    h->w_unit = (int16_t)nx_font_text_width(tf, "Hz");
    if (NX_HERO_IS_D1(font))   /* tail above "Hz" */
        h->w = (int16_t)(h->w_big + NX_HERO_TAIL_DX + (h->w_tail > h->w_unit ? h->w_tail : h->w_unit));
    else                      /* tail and "Hz" on the digits' baseline */
        h->w = (int16_t)(h->w_big + NX_HERO_TAIL_DX + (h->n_tail ? h->w_tail + NX_HERO_TAIL_DX : 0) + h->w_unit);
}

/* Decodes v into h and its digits: id (integer, no leading zeros), fd (8 decimals). */
static unsigned digits(uint64_t v, nx_hero *h, char *id, char *fd)
{
    memset(h, 0, sizeof *h);
    nx_hero_decode(v, &h->ip, &h->frac, &h->over);
    unsigned ni = 0;
    uint32_t t = h->ip;
    char rev[9];
    do {
        rev[ni++] = (char)('0' + t % 10u);
        t /= 10u;
    } while (t);
    for (unsigned i = 0; i < ni; i++)
        id[i] = rev[ni - 1u - i];
    t = h->frac;
    for (unsigned i = 8; i-- > 0;) {
        fd[i] = (char)('0' + t % 10u);
        t /= 10u;
    }
    return ni;
}

int16_t nx_hero_width(uint64_t v, uint8_t font, uint8_t k)
{
    nx_hero h;
    char id[9], fd[8];
    unsigned ni = digits(v, &h, id, fd);
    build(&h, id, ni, fd, font, k);
    return h.w;
}

int nx_hero_layout(uint64_t v, nx_hero *h)
{
    char id[9], fd[8];
    unsigned ni = digits(v, h, id, fd);
    int fits = 0;
    for (uint8_t font = NX_HERO_FIRST; font <= NX_HERO_D2 && !fits; font++) {
        for (int k = font == NX_HERO_FIRST ? 3 : 8; k >= 0; k--) {
            build(h, id, ni, fd, font, (uint8_t)k);
            if (h->w <= NX_HERO_W) {
                fits = 1;
                break;
            }
        }
    }
    unsigned ghost = 0;   /* after the last significant tail digit */
    for (unsigned i = 0; i < h->n_tail; i++)
        if (h->tail[i] >= '1' && h->tail[i] <= '9')
            ghost = i + 1u;
    h->ghost = (uint8_t)ghost;
    return fits;
}

int16_t nx_hero_height(const nx_hero *h)
{
    return big_font(h->font)->height;
}

const nx_font_t *nx_hero_font(const nx_hero *h)
{
    return big_font(h->font);
}

void nx_hero_draw(nx_gc *gc, const nx_hero *h, int16_t x, int16_t y, uint16_t c_text, uint16_t c_ghost,
                  uint16_t c_unit)
{
    const nx_font_t *bf = big_font(h->font), *tf = &nx_font_body16;
    int16_t gap = NX_HERO_IS_D1(h->font) ? NX_HERO_GAP_D1 : NX_HERO_GAP_D2;
    int16_t px = x;
    for (unsigned i = 0; i < h->n_big; i++) {
        if ((h->big_gaps >> i) & 1u)
            px = (int16_t)(px + gap);
        px = nx_text_n(gc, bf, px, y, &h->big[i], 1, c_text);
    }
    int16_t tx = (int16_t)(px + NX_HERO_TAIL_DX), ty, uy = (int16_t)(y + bf->baseline - tf->baseline);
    if (NX_HERO_IS_D1(h->font)) {   /* tail top-aligned with the big digits, "Hz" below on the baseline */
        const nx_glyph_t *g0 = nx_font_glyph(bf, '0'), *t0 = nx_font_glyph(tf, '0');
        ty = (int16_t)(y + g0->y_off - t0->y_off);
    } else {
        ty = uy;
    }
    int16_t ux = tx;
    for (unsigned i = 0; i < h->n_tail; i++) {
        if ((h->tail_gaps >> i) & 1u)
            tx = (int16_t)(tx + NX_HERO_GAP_TAIL);
        tx = nx_text_n(gc, tf, tx, ty, &h->tail[i], 1, i >= h->ghost ? c_ghost : c_text);
    }
    if (!NX_HERO_IS_D1(h->font) && h->n_tail)
        ux = (int16_t)(tx + NX_HERO_TAIL_DX);
    nx_text(gc, tf, ux, uy, "Hz", c_unit);
}
