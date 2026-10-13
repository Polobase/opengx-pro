/* nx fonts, icons and strings: lookup, row-band decode, measurement (nx_font.h). */
#include "nx_font.h"

#include "nx_assets.h"

const nx_glyph_t *nx_font_glyph(const nx_font_t *f, uint8_t code)
{
    if (f->first <= f->last && code >= f->first && code <= f->last)
        return &f->glyphs[code - f->first];
    unsigned dense = f->first <= f->last ? (unsigned)(f->last - f->first) + 1u : 0u;
    unsigned lo = 0, hi = f->n_extra;
    while (lo < hi) {   /* the extra codes are sorted: binary search */
        unsigned mid = (lo + hi) / 2u;
        uint8_t c = f->extra[mid];
        if (c == code)
            return &f->glyphs[dense + mid];
        if (c < code)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return NULL;
}

const nx_glyph_t *nx_font_glyph_or_fallback(const nx_font_t *f, uint8_t code)
{
    const nx_glyph_t *g = nx_font_glyph(f, code);
    return g ? g : nx_font_glyph(f, (uint8_t)'?');
}

static unsigned nibble(const uint8_t *d, uint32_t k)
{
    uint8_t b = d[k >> 1];
    return (k & 1u) ? (b & 15u) : (b >> 4);
}

void nx_glyph_decode(const nx_font_t *f, const nx_glyph_t *g, unsigned y0, unsigned y1, uint8_t *out)
{
    if (y1 > g->h)
        y1 = g->h;
    if (y0 >= y1 || g->w == 0)
        return;
    const uint8_t *d = f->data + (g->off & NX_GLYPH_OFF_MASK);
    uint32_t skip = (uint32_t)y0 * g->w;
    uint32_t end = (uint32_t)y1 * g->w;

    if (!(g->off & NX_GLYPH_RLE)) {
        for (uint32_t p = skip; p < end; p++) {
            if (f->bpp == 4) {
                *out++ = (uint8_t)nibble(d, p);
            } else {   /* 2 bpp: 4 pixels per byte, MSB first; value v means alpha 5*v */
                unsigned v = (d[p >> 2] >> (6u - 2u * (p & 3u))) & 3u;
                *out++ = (uint8_t)(v * 5u);
            }
        }
        return;
    }

    /* RLE: 0,n / F,n = n+1 transparent / opaque pixels; 1..E = one pixel of that alpha. Runs
     * cross row ends, so the rows above the band are walked (not written) up to `skip`. */
    uint32_t k = 0, pos = 0;
    while (pos < end) {
        unsigned a = nibble(d, k++);
        uint32_t run = 1;
        if (a == 0 || a == NX_ALPHA_MAX)
            run = nibble(d, k++) + 1u;
        uint32_t from = pos < skip ? skip : pos;
        uint32_t to = pos + run < end ? pos + run : end;
        for (uint32_t p = from; p < to; p++)
            out[p - skip] = (uint8_t)a;
        pos += run;
    }
}

unsigned nx_font_text_width_n(const nx_font_t *f, const char *s, size_t n)
{
    unsigned w = 0;
    for (size_t i = 0; i < n && s[i]; i++) {
        const nx_glyph_t *g = nx_font_glyph_or_fallback(f, (uint8_t)s[i]);
        if (g)
            w += g->advance;
    }
    return w;
}

unsigned nx_font_text_width(const nx_font_t *f, const char *s)
{
    return nx_font_text_width_n(f, s, (size_t)-1);
}

const char *nx_font_ellipsis(const nx_font_t *f)
{
    static const char k_hellip[] = { (char)NX_CH_hellip, 0 };
    if (nx_font_glyph(f, NX_CH_hellip))
        return k_hellip;
    if (nx_font_glyph(f, (uint8_t)'.'))
        return "...";
    return "";
}

nx_fit_t nx_font_fit(const nx_font_t *f, const char *s, unsigned max_px)
{
    nx_fit_t r = { 0, 0, 0 };
    size_t len = 0;
    while (s[len])
        len++;
    unsigned full = nx_font_text_width_n(f, s, len);
    if (full <= max_px) {
        r.n = (uint16_t)len;
        r.width = (uint16_t)full;
        return r;
    }
    unsigned ell = nx_font_text_width(f, nx_font_ellipsis(f));
    if (ell == 0 || ell > max_px)
        return r;
    unsigned w = 0;
    size_t n = 0;
    for (; n < len; n++) {
        const nx_glyph_t *g = nx_font_glyph_or_fallback(f, (uint8_t)s[n]);
        unsigned a = g ? g->advance : 0u;
        if (w + a + ell > max_px)
            break;
        w += a;
    }
    while (n > 0 && s[n - 1] == ' ') {   /* "Program 1…", not "Program …" */
        n--;
        w -= nx_font_text_width_n(f, " ", 1);
    }
    r.n = (uint16_t)n;
    r.width = (uint16_t)(w + ell);
    r.ellipsis = 1;
    return r;
}

const char *nx_str(unsigned id)
{
    if (id >= NX_STR_COUNT)
        return "";
    return nx_str_pool + nx_str_off[NX_LANG_EN][id];
}

size_t nx_str_format(char *out, size_t size, unsigned id, const char *const *args, unsigned nargs)
{
    if (size == 0)
        return 0;
    size_t n = 0;
    for (const char *s = nx_str(id); *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= 1u && c <= NX_STR_ARGS) {
            const char *a = (c - 1u < nargs && args[c - 1u]) ? args[c - 1u] : "";
            while (*a && n + 1 < size)
                out[n++] = *a++;
        } else if (n + 1 < size) {
            out[n++] = (char)c;
        }
    }
    out[n] = 0;
    return n;
}
