/* Formatting helpers and the waveform map (nx_uifmt.h). */
#include "nx_uifmt.h"

#include "nx_assets.h"
#include "nx_font.h"
#include "nx_hero.h"
#include "nx_scope.h"

static char *put(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

char *nx_fmt_u(char *p, uint32_t v)
{
    char tmp[10];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

char *nx_fmt_2d(char *p, uint32_t v)
{
    *p++ = (char)('0' + v / 10u % 10u);
    *p++ = (char)('0' + v % 10u);
    *p = 0;
    return p;
}

char *nx_fmt_dur(char *p, uint32_t s)
{
    uint32_t h = s / 3600u, m = s / 60u % 60u;
    if (h) {
        p = nx_fmt_u(p, h);
        *p++ = ':';
        p = nx_fmt_2d(p, m);
    } else {
        p = nx_fmt_u(p, m);
    }
    *p++ = ':';
    return nx_fmt_2d(p, s % 60u);
}

char *nx_fmt_cv(char *p, uint32_t cv)
{
    uint32_t dv = (cv + 5u) / 10u;   /* tenths of a volt */
    p = nx_fmt_u(p, dv / 10u);
    *p++ = '.';
    *p++ = (char)('0' + dv % 10u);
    *p++ = ' ';
    return put(p, nx_str(NX_S_UNIT_V));
}

char *nx_fmt_pct(char *p, uint32_t pct)
{
    p = nx_fmt_u(p, pct);
    *p++ = ' ';
    *p++ = '%';
    *p = 0;
    return p;
}

char *nx_fmt_hex(char *p, uint32_t v, unsigned digits)
{
    for (unsigned i = digits; i-- > 0;)
        *p++ = "0123456789ABCDEF"[(v >> (4u * i)) & 15u];
    *p = 0;
    return p;
}

char *nx_fmt_delay(char *p, uint32_t s)
{
    char v[8];
    const char *args[1] = { v };
    if (!s)
        return put(p, nx_str(NX_S_COMMON_NEVER));
    nx_fmt_u(v, s < 60u ? s : s / 60u);
    return p + nx_str_format(p, 16, s < 60u ? NX_S_SET_SECONDS : NX_S_SET_MINUTES, args, 1);
}

char *nx_fmt_freq(char *p, uint64_t v, unsigned min_dec, unsigned max_dec, int group_dec)
{
    uint32_t ip, frac;
    uint8_t over;
    nx_hero_decode(v, &ip, &frac, &over);
    char id[10];
    unsigned ni = 0;
    do {
        id[ni++] = (char)('0' + ip % 10u);
        ip /= 10u;
    } while (ip);
    for (unsigned i = ni; i-- > 0;) {
        *p++ = id[i];
        if (i && i % 3u == 0)
            *p++ = ' ';
    }
    char fd[8];
    for (unsigned i = 8; i-- > 0;) {
        fd[i] = (char)('0' + frac % 10u);
        frac /= 10u;
    }
    if (max_dec > 8u)
        max_dec = 8u;
    unsigned nd = max_dec;
    while (nd > min_dec && fd[nd - 1u] == '0')
        nd--;
    if (nd) {
        *p++ = '.';
        for (unsigned i = 0; i < nd; i++) {
            if (group_dec && i && i % 3u == 0)
                *p++ = ' ';
            *p++ = fd[i];
        }
    }
    *p = 0;
    return p;
}

char *nx_fmt_pc_freq(char *p, uint64_t v, unsigned mult)
{
    if (mult <= 1u)
        return nx_fmt_freq(p, v, 2, 8, 0);
    uint32_t ip, frac;
    uint8_t over;
    nx_hero_decode(v, &ip, &frac, &over);
    /* in 1e-8 Hz, half of 1e-6 Hz added: cut after 6 decimals it is rounded. mult is at most
     * NX_PCMON_MAX_CYCLES (nx_diag.h), so the code (times 10, exponent digit 0) fits 64 bits */
    uint64_t n = ((uint64_t)ip * 100000000u + frac) * mult + 50u;
    return nx_fmt_freq(p, n * 10u, 2, 6, 0);
}

int nx_wave_info(uint8_t code, struct nx_wave_info *wi)
{
    static const struct nx_wave_info k_spooky[9] = {   /* codes 11..19: Spooky2 waveforms 1..9 */
        { NX_WAVE_SINE, NX_OUT2_SYNC, NX_S_WAVE_SINE },
        { NX_WAVE_SQUARE, NX_OUT2_SYNC, NX_S_WAVE_SQUARE },
        { NX_WAVE_SAW, NX_OUT2_SYNC, NX_S_WAVE_SAW },
        { NX_WAVE_INV_SAW, NX_OUT2_SYNC, NX_S_WAVE_INV_SAW },
        { NX_WAVE_TRIANGLE, NX_OUT2_SYNC, NX_S_WAVE_TRIANGLE },
        { NX_WAVE_DAMPED_SINE, NX_OUT2_SYNC, NX_S_WAVE_DAMPED_SINE },
        { NX_WAVE_DAMPED_SQUARE, NX_OUT2_SYNC, NX_S_WAVE_DAMPED_SQUARE },
        { NX_WAVE_HBOMB_SINE, NX_OUT2_SYNC, NX_S_WAVE_HBOMB_SINE },       /* 18: Spooky2's Sine_Hbomb */
        { NX_WAVE_HBOMB_EDGE, NX_OUT2_SYNC, NX_S_WAVE_HBOMB_SQUARE },     /* 19: Square_Hbomb */
    };
    if (code == 0) {
        *wi = (struct nx_wave_info){ NX_WAVE_FLAT, NX_OUT2_SYNC, NX_WAVE_NO_NAME };
        return 1;
    }
    if (code >= 11u && code <= 19u) {
        *wi = k_spooky[code - 11u];
        return 1;
    }
    if (code == 25u) {
        *wi = (struct nx_wave_info){ NX_WAVE_SINE, NX_OUT2_INVERSE, NX_S_OUT_INVERSE };
        return 1;
    }
    *wi = (struct nx_wave_info){ NX_WAVE_SINE, NX_OUT2_SYNC, NX_WAVE_NO_NAME };   /* shown as "wave #N ?" */
    return 0;
}

static char *sep(char *p)   /* " · " */
{
    *p++ = ' ';
    *p++ = (char)NX_CH_middot;
    *p++ = ' ';
    return p;
}

char *nx_fmt_summary(char *out, const struct nx_slot_info *si)
{
    char v[16];   /* the longest dwell: "1193046:28:15" */
    const char *args[1] = { v };
    nx_fmt_u(v, si->count);
    char *p = sep(out + nx_str_format(out, 24, NX_S_LIB_STEPS_N, args, 1));
    nx_fmt_dur(v, si->dwell_s);
    p = sep(p + nx_str_format(p, 32, NX_S_DASH_PER_STEP, args, 1));
    struct nx_wave_info wi;
    if (nx_wave_info(si->wave, &wi) && wi.name != NX_WAVE_NO_NAME) {
        p = put(p, nx_str(wi.name));
    } else {
        nx_fmt_u(v, si->wave);
        p += nx_str_format(p, 24, NX_S_DASH_WAVE_UNKNOWN, args, 1);
    }
    return nx_fmt_cv(sep(p), si->ampl_cv);
}
