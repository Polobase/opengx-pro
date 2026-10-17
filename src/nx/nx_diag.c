/* nx error and health counters (nx_diag.h). */
#include "nx_diag.h"

#include "nx_bfb.h"

#include <string.h>

#include "nx_config.h"
#include "state.h"

struct nx_diag g_nx_diag;
struct nx_pcmon g_nx_pcmon;

/* ":wNN=v," and ":aNN=v,v,..." per link, a byte at a time (the receive interrupt: no line
 * buffer; one bounded loop at the end of a table, tab_cycles) */
struct rx {
    uint8_t st, cmd;
    uint8_t tab;         /* an ":a" line: its samples are counted */
    uint8_t hi, hi0;     /* the last and the first sample are >= 512 */
    uint16_t idx;        /* samples so far */
    uint16_t n;          /* upward crossings of 512 */
    uint16_t x0, x1;     /* the first and the last crossing */
    uint16_t gmin, gmax; /* the shortest and the longest gap between two crossings */
    uint16_t gap[NX_PCMON_GAPS];   /* the first gaps, for the grid rule */
    uint32_t v;
};
static struct rx s_rx[2];

static uint32_t s_last_pass_ms;
static uint8_t s_loop_seen;

void nx_diag_reset(void)
{
    memset(&g_nx_diag, 0, sizeof g_nx_diag);
    memset(&g_nx_pcmon, 0, sizeof g_nx_pcmon);
    memset(s_rx, 0, sizeof s_rx);
    s_loop_seen = 0;
}

static void pcmon_set(unsigned ch, unsigned cmd, uint32_t v)
{
    struct nx_pcmon *m = &g_nx_pcmon;
    unsigned out = cmd & 1u;   /* w20 / w28: Out1, w21 / w29: Out2 */
    switch (cmd) {
    case 20:
    case 21:
        m->wave[ch][out] = (uint8_t)(v > 255u ? 255u : v);
        m->seen[ch] |= out ? NX_PCS_WAVE2 : NX_PCS_WAVE1;
        break;
    case 28:
    case 29:
        m->ampl_cv[ch][out] = (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
        m->seen[ch] |= out ? NX_PCS_AMPL2 : NX_PCS_AMPL1;
        break;
    case 12:
        m->gate = (uint8_t)(v ? m->gate | (1u << ch) : m->gate & ~(1u << ch));
        m->seen[ch] |= NX_PCS_GATE;
        break;
    default:
        break;
    }
}

/* An upward crossing of 512 at sample i of the table. */
static void tab_cross(struct rx *r, unsigned i)
{
    if (r->n) {
        unsigned gap = i - r->x1;
        if (gap < r->gmin)
            r->gmin = (uint16_t)gap;
        if (gap > r->gmax)
            r->gmax = (uint16_t)gap;
        if (r->n <= NX_PCMON_GAPS)
            r->gap[r->n - 1u] = (uint16_t)gap;
    } else {
        r->x0 = (uint16_t)i;
    }
    r->x1 = (uint16_t)i;
    r->n++;
}

/* The cycles of the table just read, from its n crossings and their n gaps (nx_diag.h). */
static unsigned tab_cycles(const struct rx *r, unsigned n)
{
    if (n >= 2u && n <= NX_PCMON_MAX_CYCLES && r->gmax <= r->gmin + NX_PCMON_GAP_TOL)
        return n;
    if (n < NX_PCMON_GRID_MIN || n > NX_PCMON_GAPS)
        return 1u;
    /* the shortest gap is one step of the grid; a sample or two off, it can name a neighbour */
    unsigned c0 = (NX_PCMON_SAMPLES + r->gmin / 2u) / r->gmin;
    for (unsigned i = 0; i < 3u; i++) {
        unsigned c = c0 + (i == 2u) - (i == 1u), sum = 0, j;
        if (c < 2u || c > NX_PCMON_MAX_CYCLES)
            continue;
        for (j = 0; j < n; j++) {   /* every gap is k steps of 1024 / c samples */
            unsigned p = r->gap[j] * c, k = (p + NX_PCMON_SAMPLES / 2u) / NX_PCMON_SAMPLES;
            unsigned q = k * NX_PCMON_SAMPLES;
            if ((p > q ? p - q : q - p) > NX_PCMON_GAP_TOL * c)
                break;
            sum += k;
        }
        if (j == n && sum == c)
            return c;
    }
    return 1u;
}

void nx_rx_tap(unsigned ch, uint8_t c)
{
    ch &= 1u;
    nx_bfb_rx(ch, c);   /* the generator's replies to the scan's read lines */
    /* link.c discards the byte while its link has a line pending (the :ok matcher still runs). */
    if (g_ui_flags & UIF_LINE(ch))
        g_nx_diag.rx_dropped[ch]++;
    struct rx *r = &s_rx[ch];
    unsigned d = (unsigned)c - '0', st = r->st;
    r->st = 0;
    if (c == ':') {
        r->st = 1;
    } else if (st == 1u) {
        r->tab = c == 'a';
        if (c == 'w' || c == 'a')
            r->st = 2;
    } else if (st == 2u || st == 3u) {
        if (d <= 9u) {
            r->cmd = (uint8_t)(st == 2u ? d * 10u : r->cmd + d);
            r->st = (uint8_t)(st + 1u);
        }
    } else if (st == 4u) {
        if (c == '=')
            r->st = 5;
        r->v = 0;
        r->idx = r->n = 0;
        r->gmin = 0xFFFFu;
        r->gmax = 0;
    } else if (st == 5u) {
        if (d <= 9u) {
            if (r->v < 100000000u)
                r->v = r->v * 10u + d;
            r->st = 5;
        } else if (!r->tab) {
            if (c == ',')
                pcmon_set(ch, r->cmd, r->v);
        } else if (c == ',') {   /* one sample of the table */
            uint8_t hi = r->v >= NX_PCMON_SAMPLES / 2u;
            if (!r->idx)
                r->hi0 = hi;
            else if (hi && !r->hi)
                tab_cross(r, r->idx);
            r->hi = hi;
            if (r->idx <= NX_PCMON_SAMPLES)
                r->idx++;
            r->v = 0;
            r->st = 5;
        } else {   /* the end of the line: a whole table's cycles are kept */
            unsigned t = (unsigned)r->cmd - NX_PCMON_TAB0;
            if (r->idx == NX_PCMON_SAMPLES && t < NX_PCMON_TABS) {
                if (!r->hi && r->hi0)   /* from the last sample up to the first */
                    tab_cross(r, NX_PCMON_SAMPLES);
                unsigned n = r->n;
                if (n)   /* the gap around the end of the table */
                    tab_cross(r, r->x0 + NX_PCMON_SAMPLES);
                g_nx_pcmon.cycles[ch][t] = (uint8_t)tab_cycles(r, n);
            }
        }
    }
}

void nx_diag_loop(uint32_t now_ms)
{
    if (s_loop_seen) {
        uint32_t gap = now_ms - s_last_pass_ms;
        if (gap > g_nx_diag.loop_gap_max_ms)
            g_nx_diag.loop_gap_max_ms = gap;
    }
    s_last_pass_ms = now_ms;
    s_loop_seen = 1;
}

void nx_diag_isr_time(uint16_t us)
{
    if (us > g_nx_diag.isr_max_us)
        g_nx_diag.isr_max_us = us;
}

int nx_diag_sensors(uint16_t ts, uint16_t vref, uint16_t ts_cal, uint16_t vref_cal, int16_t *t10, uint16_t *vdd_mv)
{
    if (!vref)
        return 0;
    uint32_t vdda = 3000u * vref_cal / vref;                      /* mV */
    int32_t ts30 = (int32_t)((uint32_t)ts * vdda / 3000u);        /* the reading at 3.0 V */
    *t10 = (int16_t)(300 + (ts30 - (int32_t)ts_cal) * 12000 / 4095);   /* 3000 mV / 4095 / 2.5 mV per degC */
    *vdd_mv = (uint16_t)vdda;
    return 1;
}
