/* Waveform preview (nx_scope.h). The NX_SCOPE_* switches (nx_config.h) are plain constants in
 * the conditions: a build that turns a feature off loses its code to constant folding. */
#include "nx_scope.h"

#include "nx_anim.h"
#include "nx_font.h"

/* burst envelope exp(-i/6), Q12, over one period */
static const uint16_t k_env[32] = { 4096, 3467, 2935, 2484, 2103, 1780, 1507, 1276, 1080, 914, 774,
                                    655,  554,  469,  397,  336,  285,  241,  204,  173,  146, 124,
                                    105,  89,   75,   64,   54,   46,   39,   33,   28,   23 };

static int32_t sine12(uint32_t ph)
{
    return nx_sin1024(ph >> 6) >> 2;
}

static int32_t square12(uint32_t ph)
{
    return (ph & 0x8000u) ? -4096 : 4096;
}

int32_t nx_wave_sample(uint8_t wave, const int8_t *custom, uint32_t ph)
{
    ph &= 0xFFFFu;
    int32_t env = k_env[ph >> 11];
    switch (wave) {
    case NX_WAVE_SINE:
        return sine12(ph);
    case NX_WAVE_SQUARE:
        return square12(ph);
    case NX_WAVE_SAW:
        return (int32_t)(ph >> 3) - 4096;
    case NX_WAVE_INV_SAW:
        return 4096 - (int32_t)(ph >> 3);
    case NX_WAVE_TRIANGLE:
        return ph < 0x8000u ? (int32_t)(ph >> 2) - 4096 : 4096 - (int32_t)((ph - 0x8000u) >> 2);
    case NX_WAVE_DAMPED_SINE:   /* a burst of 4 carrier cycles decaying over the period */
        return (sine12(ph * 4u) * env) >> 12;
    case NX_WAVE_DAMPED_SQUARE:
        return (square12(ph * 4u) * env) >> 12;
    case NX_WAVE_HBOMB_EDGE: {  /* square with ringing after each edge (approximation) */
        uint32_t half = (ph & 0x7FFFu) << 1;
        return (square12(ph) * 5 >> 3) + ((sine12(half * 6u) * (int32_t)k_env[half >> 11]) >> 12) * 3 / 8;
    }
    case NX_WAVE_HBOMB_SINE:    /* sine with a ringing burst (approximation) */
        return (sine12(ph) * 5 >> 3) + ((sine12(ph * 8u) * env) >> 12) * 3 / 8;
    case NX_WAVE_CUSTOM: {
        if (!NX_SCOPE_CUSTOM || !custom)
            return 0;
        uint32_t i = ph >> 10, f = ph & 1023u;
        int32_t a = custom[i], b = custom[(i + 1u) & 63u];
        return ((a * 1024 + (b - a) * (int32_t)f) * 32) >> 10;   /* 127 -> 4064 */
    }
    default:
        return 0;
    }
}

uint16_t nx_scope_phase(uint32_t t_ms)
{
    if (nx_anim_reduced())
        return 0;
    uint32_t t = t_ms - t_ms % NX_SCOPE_FRAME_MS;
    return (uint16_t)((t % 2000u) * 4096u / 125u);   /* 65536 / 2000 ms: 0.5 cycles/s */
}

struct trace {
    uint8_t wave, inv;
    const int8_t *custom;
    uint32_t ph0, dph;
    int32_t cy, amp;             /* Q4 */
    int16_t n, gate_period, gate_on;
};

static int32_t trace_y(void *ctx, int16_t i)
{
    const struct trace *t = ctx;
    if (i < 0)
        i = 0;
    if (i >= t->n)
        i = (int16_t)(t->n - 1);
    int32_t s = 0;
    if (!NX_SCOPE_GATE || !t->gate_period || i % t->gate_period < t->gate_on)
        s = nx_wave_sample(t->wave, t->custom, t->ph0 + (uint32_t)i * t->dph);
    if (t->inv)
        s = -s;
    return t->cy - ((s * t->amp) >> 12);
}

static void fmt_cv(char *p, uint16_t cv)   /* 2000 -> "20.0 V" */
{
    char tmp[8];
    unsigned n = 0, v = (cv + 5u) / 10u;   /* 0.1 V */
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    if (n == 1)
        tmp[n++] = '0';
    for (unsigned i = n; i-- > 0;) {
        *p++ = tmp[i];
        if (i == 1)
            *p++ = '.';
    }
    *p++ = ' ';
    const char *u = nx_str(NX_S_UNIT_V);
    while (*u)
        *p++ = *u++;
    *p = 0;
}

static void fmt_ratio(char *p, uint16_t q8)   /* Q8 -> "×1.50" */
{
    uint32_t h = ((uint32_t)q8 * 100u + 128u) >> 8;   /* hundredths */
    *p++ = (char)NX_CH_times;
    char tmp[8];
    unsigned n = 0;
    uint32_t v = h / 100u;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p++ = '.';
    *p++ = (char)('0' + (h / 10u) % 10u);
    *p++ = (char)('0' + h % 10u);
    *p = 0;
}

void nx_scope_draw(nx_gc *gc, nx_rect r, const nx_scope_cfg *cfg, uint16_t phase)
{
    nx_rrect(gc, r, 8, nx_col(gc, NX_PAL_SURFACE1));
    nx_rect p = { (int16_t)(r.x0 + 4), (int16_t)(r.y0 + 4), (int16_t)(r.x1 - 4), (int16_t)(r.y1 - 4) };
    int16_t n = (int16_t)(p.x1 - p.x0), h = (int16_t)(p.y1 - p.y0);
    if (n <= 0 || h <= 6)   /* no room for a trace (and n divides the phase step below) */
        return;
    int16_t gp = NX_SCOPE_GATE && cfg->gate_pct ? (int16_t)(n * 2 / 5) : 0, gon = (int16_t)(gp * cfg->gate_pct / 100);
    uint16_t grid = nx_col(gc, NX_PAL_GRID);

    for (int16_t x = 0; gp && x < n; x = (int16_t)(x + gp))   /* gate: "on" windows shaded */
        nx_fill(gc, (nx_rect){ (int16_t)(p.x0 + x), p.y0, (int16_t)(p.x0 + (x + gon < n ? x + gon : n)), p.y1 },
                nx_col(gc, NX_PAL_SURFACE2));
    for (int k = 1; k < 4; k++) {                               /* dotted 4 x 2 grid */
        int16_t gx = (int16_t)(p.x0 + n * k / 4);
        for (int16_t y = p.y0; y < p.y1; y = (int16_t)(y + 3))
            nx_fill(gc, (nx_rect){ gx, y, (int16_t)(gx + 1), (int16_t)(y + 1) }, grid);
    }
    int16_t zy = (int16_t)(p.y0 + h / 2);
    nx_fill(gc, (nx_rect){ p.x0, zy, p.x1, (int16_t)(zy + 1) }, grid);   /* zero line */

    uint16_t c = cfg->state == NX_SCOPE_PAUSED ? nx_col(gc, NX_PAL_TEXT2)
               : cfg->state == NX_SCOPE_STOPPED ? nx_col(gc, NX_PAL_TEXT3) : nx_col(gc, cfg->accent);
    uint8_t wave = cfg->state == NX_SCOPE_STOPPED ? NX_WAVE_FLAT : cfg->wave;
    struct trace t1 = { wave, 0, cfg->custom, phase, 163840u / (uint32_t)n, (int32_t)(zy * 16 + 8),
                        (int32_t)(h / 2 - 3) * 16, n, gp, gon };
    nx_rect saved = nx_clip_push(gc, p);
    if (cfg->out2 == NX_OUT2_INVERSE || (NX_SCOPE_RATIO && cfg->out2 == NX_OUT2_INDEPENDENT)) {
        struct trace t2 = t1;
        if (!NX_SCOPE_RATIO || cfg->out2 == NX_OUT2_INVERSE) {
            t2.inv = 1;
        } else {
            uint32_t q = cfg->ratio_q8 < NX_SCOPE_RATIO_MIN ? NX_SCOPE_RATIO_MIN
                       : cfg->ratio_q8 > NX_SCOPE_RATIO_MAX ? NX_SCOPE_RATIO_MAX : cfg->ratio_q8;
            t2.wave = cfg->state == NX_SCOPE_STOPPED ? NX_WAVE_FLAT : cfg->wave2;
            t2.custom = cfg->custom2;
            t2.dph = (t1.dph * q) >> 8;
            t2.ph0 = (t1.ph0 * q) >> 8;
        }
        nx_trace(gc, p.x0, n, trace_y, &t2, 12, c, 19);        /* 1.5 px at 60 % */
    }
    nx_trace(gc, p.x0, n, trace_y, &t1, 32, c, 6);             /* glow: 4 px at 20 % */
    nx_trace(gc, p.x0, n, trace_y, &t1, 16, c, 32);            /* 2 px */
    nx_clip_pop(gc, saved);

    const nx_font_t *f = &nx_font_cap13;
    uint16_t lc = nx_col(gc, NX_PAL_TEXT2);
    char buf[24];
    if (NX_SCOPE_LABELS && cfg->ampl_cv) {
        fmt_cv(buf, cfg->ampl_cv);
        nx_text(gc, f, (int16_t)(p.x0 + 2), p.y0, buf, lc);
    }
    const char *rel = 0;
    if (cfg->out2 == NX_OUT2_SYNC)
        rel = "1 = 2";
    if (cfg->out2 == NX_OUT2_INVERSE) {
        static const char k_inv[] = { '2', ' ', '=', ' ', (char)NX_CH_ndash, '1', 0 };
        rel = k_inv;
    }
    if (NX_SCOPE_RATIO && cfg->out2 == NX_OUT2_INDEPENDENT) {
        fmt_ratio(buf, cfg->ratio_q8);
        rel = buf;
    }
    if (rel)
        nx_text(gc, f, (int16_t)(p.x1 - 2 - (int16_t)nx_font_text_width(f, rel)), p.y0, rel, lc);
    int16_t by = (int16_t)(p.y1 - f->height);
    if (NX_SCOPE_GATE && cfg->gate_pct) {
        char g[8];
        unsigned v = cfg->gate_pct, i = 0;
        if (v >= 100u)
            g[i++] = '1';
        if (v >= 10u)
            g[i++] = (char)('0' + (v / 10u) % 10u);
        g[i++] = (char)('0' + v % 10u);
        g[i++] = ' ';
        g[i++] = '%';
        g[i] = 0;
        nx_text(gc, f, (int16_t)(p.x1 - 2 - (int16_t)nx_font_text_width(f, g)), by, g, lc);
    }
    if (NX_SCOPE_LABELS && cfg->unverified) {
        char code[4];
        unsigned v = cfg->wave_code, i = 0;
        if (v >= 100u)
            code[i++] = (char)('0' + v / 100u);
        if (v >= 10u)
            code[i++] = (char)('0' + (v / 10u) % 10u);
        code[i++] = (char)('0' + v % 10u);
        code[i] = 0;
        const char *args[1] = { code };
        nx_str_format(buf, sizeof buf, NX_S_DASH_WAVE_UNKNOWN, args, 1);
        nx_text(gc, f, (int16_t)(p.x0 + 2), by, buf, nx_col(gc, NX_PAL_WARNING));
    }
}
