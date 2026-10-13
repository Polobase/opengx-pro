/* nx time base, PB1 debounce and the diagnostics counters (src/nx/nx_time.c, nx_diag.c). */
#include "harness.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_time.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

/* ":aNN=" + 1024 samples made by fn(i) + CR LF: a wave table upload, as Spooky2 sends it */
static char s_table[6000];

static const char *table(unsigned nn, int (*fn)(unsigned), unsigned samples)
{
    int n = snprintf(s_table, sizeof s_table, ":a%02u=", nn);
    for (unsigned i = 0; i < samples; i++)
        n += snprintf(s_table + n, sizeof s_table - (size_t)n, "%d,", fn(i));
    snprintf(s_table + n, sizeof s_table - (size_t)n, "\r\n");
    return s_table;
}

static const double k_tau = 6.283185307179586;

static int sine11(unsigned i)   /* 11 cycles at half scale: what a Wave Cycle Multiplier of 11 uploads */
{
    return (int)floor(512.0 + 255.75 * sin(k_tau * 11.0 * i / 1024.0));
}

static int sine11_inv(unsigned i)
{
    return 1023 - sine11(i);
}

static int sine1(unsigned i)
{
    return (int)floor(511.5 + 511.5 * sin(k_tau * i / 1024.0) + 0.5);
}

static int square11(unsigned i)
{
    return sine11(i) >= 512 ? 767 : 256;
}

static int ramp(unsigned i)
{
    return (int)((i + 1u) & 1023u);
}

static int flat(unsigned i)
{
    (void)i;
    return 512;
}

static int bursts(unsigned i)   /* twice 4 pulses 64 apart and a pause: 8 crossings on a grid of 16 */
{
    unsigned k = i % 512u;
    return k < 256u && (k / 32u) % 2u ? 900 : 100;
}

static int bursts_first(unsigned i)   /* 8 pulses 64 apart, then none to the end */
{
    return i < 512u && (i / 32u) % 2u ? 900 : 100;
}

/* The shape of Spooky2's H-Bomb tables: 16 cycle slots, 4 flat, a full cycle, 6 small ones, a
 * full cycle, 4 flat. */
static int hbomb16(unsigned i)
{
    unsigned slot = i / 64u;
    double a = slot < 4u || slot > 11u ? 0.0 : slot == 4u || slot == 11u ? 511.5 : 102.5;
    return (int)floor(512.0 + a * sin(k_tau * (i % 64u) / 64.0));
}

static int hbomb11(unsigned i)   /* the same idea on 11 slots (93.09 samples each): 5 cycles, then flat */
{
    double ph = 11.0 * i / 1024.0;
    return ph >= 3.0 && ph < 8.0 ? (int)floor(512.0 + 300.0 * sin(k_tau * ph)) : 512;
}

static int pair(unsigned i)   /* two pulses 64 apart: too few to trust a grid */
{
    return (i >= 100u && i < 132u) || (i >= 164u && i < 196u) ? 900 : 100;
}

static int scattered(unsigned i)   /* three pulses on no grid */
{
    return (i >= 100u && i < 140u) || (i >= 250u && i < 290u) || (i >= 700u && i < 740u) ? 900 : 100;
}

static int damped11(unsigned i)   /* 11 cycles that fade: still evenly spaced */
{
    return (int)floor(512.0 + 400.0 * exp(-(double)i / 600.0) * sin(k_tau * 11.0 * i / 1024.0));
}

void test_nx_time(void)
{
    /* SysTick runs from the simulated clock: nx_ms follows it, g_nx_ms counts the ticks */
    nxw_reset();
    CHECK_EQ(nx_ms(), 0);
    sim_advance_ms(25);
    CHECK_EQ(nx_ms(), 25);
    CHECK_EQ(g_nx_ms, 25);
    sim_advance_us(1500);
    CHECK_EQ(nx_us16(), (uint16_t)26500u);

    /* PB1: a new level counts after NX_PB1_DEBOUNCE_MS (15) samples in a row */
    nxw_reset();
    CHECK_EQ(nx_pb1(), 0);
    nxw_pb1 = 1;
    sim_advance_ms(14);
    CHECK_EQ(nx_pb1(), 0);
    sim_advance_ms(1);
    CHECK_EQ(nx_pb1(), 1);
    /* a 14 ms glitch is ignored, also when it repeats */
    for (int i = 0; i < 3; i++) {
        nxw_pb1 = 0;
        sim_advance_ms(14);
        nxw_pb1 = 1;
        sim_advance_ms(3);
    }
    CHECK_EQ(nx_pb1(), 1);
    nxw_pb1 = 0;
    sim_advance_ms(15);
    CHECK_EQ(nx_pb1(), 0);
    /* PB1 high at reset: already high, no debounce needed */
    nxw_reset();
    nxw_pb1 = 1;
    nx_time_reset();
    CHECK_EQ(nx_pb1(), 1);

    /* diag: longest gap between loop passes, longest handler, bytes dropped while a line waits */
    nxw_reset();
    nx_diag_loop(100);
    nx_diag_loop(103);
    nx_diag_loop(140);
    nx_diag_loop(141);
    CHECK_EQ(g_nx_diag.loop_gap_max_ms, 37);
    nx_diag_isr_time(12);
    nx_diag_isr_time(7);
    CHECK_EQ(g_nx_diag.isr_max_us, 12);
    /* MCU sensors from the factory calibration: 30.0 degC at 3.30 V (the emulator's readings);
     * the same sensor voltage at 3.0 V; a colder sensor; no VREFINT reading */
    int16_t t10 = 0;
    uint16_t mv = 0;
    CHECK(nx_diag_sensors(943, 1505, 1037, 1656, &t10, &mv));
    CHECK_EQ(mv, 3300);
    CHECK_EQ(t10, 300);
    CHECK(nx_diag_sensors(1037, 1656, 1037, 1656, &t10, &mv));
    CHECK_EQ(mv, 3000);
    CHECK_EQ(t10, 300);
    CHECK(nx_diag_sensors(1003, 1656, 1037, 1656, &t10, &mv));   /* 34 LSB = 24.9 mV below */
    CHECK_EQ(t10, 201);
    CHECK(!nx_diag_sensors(943, 0, 1037, 1656, &t10, &mv));
    nxw_rx(0, ":n00=$\r\n");                 /* the tap counts only bytes the parser drops */
    CHECK_EQ(g_nx_diag.rx_dropped[0], 0);
    CHECK(g_ui_flags & UIF_LINE_G1);
    nxw_rx(0, ":w24=5,\r\n");
    CHECK_EQ(g_nx_diag.rx_dropped[0], 9);
    CHECK_EQ(g_nx_diag.rx_dropped[1], 0);

    /* the PC monitor's tap parser: waveform, amplitude and gating per output, from the bytes as
     * they arrive (a line split anywhere); anything else, a reply or a broken line changes nothing */
    nxw_reset();
    nxw_rx(1, ":w20=12,\r\n:w2");
    CHECK_EQ(g_nx_pcmon.seen[1], NX_PCS_WAVE1);   /* Out2's values are not there yet */
    nxw_rx(1, "8=1500,\r\n:w21=25,\r\n:w29=600,\r\n:w12=1,1,\r\n:w32=20,\r\n:w33=220,\r\n");
    nxw_rx(0, ":w91=1,\r\n:w2x=5,\r\n:w20=7\r\n:w28=99999999999,\r\n:r20=3,\r\n:r32=20,\r\n");
    CHECK(g_nx_pcmon.wave[1][0] == 12 && g_nx_pcmon.wave[1][1] == 25);
    CHECK(g_nx_pcmon.ampl_cv[1][0] == 1500 && g_nx_pcmon.ampl_cv[1][1] == 600);
    CHECK(g_nx_pcmon.gate == 2);
    CHECK_EQ(g_nx_pcmon.seen[1], NX_PCS_WAVE1 | NX_PCS_AMPL1 | NX_PCS_GATE | NX_PCS_WAVE2 | NX_PCS_AMPL2);
    CHECK(g_nx_pcmon.wave[0][0] == 0 && g_nx_pcmon.ampl_cv[0][0] == 0xFFFF && g_nx_pcmon.seen[0] == NX_PCS_AMPL1);
    nxw_rx(1, ":w12=0,0,\r\n");
    CHECK_EQ(g_nx_pcmon.gate, 0);

    /* wave table uploads: the cycles of a table are its evenly spaced upward crossings of 512,
     * or the steps of the one grid three or more crossings lie on */
    nxw_reset();
    nxw_rx(0, table(11, sine11, 1024));
    CHECK_EQ(g_nx_pcmon.cycles[0][0], 11);
    CHECK_EQ(g_nx_pcmon.cycles[1][0], 0);          /* the other link: nothing uploaded */
    nxw_rx(0, table(25, sine11_inv, 1024));        /* starts below 512: no crossing around the end */
    CHECK_EQ(g_nx_pcmon.cycles[0][14], 11);
    nxw_rx(0, table(12, square11, 1024));
    CHECK_EQ(g_nx_pcmon.cycles[0][1], 11);
    nxw_rx(0, table(16, damped11, 1024));
    CHECK_EQ(g_nx_pcmon.cycles[0][5], 11);
    nxw_rx(0, table(13, ramp, 1024));              /* one crossing: one cycle */
    CHECK_EQ(g_nx_pcmon.cycles[0][2], 1);
    nxw_rx(0, table(18, hbomb16, 1024));           /* Spooky2's H-Bomb shape: 8 cycles in 16 slots */
    CHECK_EQ(g_nx_pcmon.cycles[0][7], 16);
    nxw_rx(0, table(19, bursts_first, 1024));      /* the same grid, the pause around the table's end */
    CHECK_EQ(g_nx_pcmon.cycles[0][8], 16);
    nxw_rx(0, table(20, bursts, 1024));            /* two pauses */
    CHECK_EQ(g_nx_pcmon.cycles[0][9], 16);
    nxw_rx(0, table(21, hbomb11, 1024));           /* a grid step that is not a whole number of samples */
    CHECK_EQ(g_nx_pcmon.cycles[0][10], 11);
    nxw_rx(0, table(22, pair, 1024));              /* two crossings fit a grid by chance: one cycle */
    CHECK_EQ(g_nx_pcmon.cycles[0][11], 1);
    nxw_rx(0, table(23, scattered, 1024));         /* three on no grid */
    CHECK_EQ(g_nx_pcmon.cycles[0][12], 1);
    nxw_rx(0, table(24, flat, 1024));              /* no crossing */
    CHECK_EQ(g_nx_pcmon.cycles[0][13], 1);
    nxw_rx(1, table(11, sine1, 1024));             /* an ordinary sine, on the other link */
    CHECK_EQ(g_nx_pcmon.cycles[1][0], 1);
    CHECK_EQ(g_nx_pcmon.cycles[0][0], 11);
    nxw_rx(0, table(11, sine1, 1000));             /* not a whole table: the last one stays */
    CHECK_EQ(g_nx_pcmon.cycles[0][0], 11);
    nxw_rx(0, table(27, sine11, 1024));            /* tables outside 11..26 are not kept */
    nxw_rx(0, table(10, sine11, 1024));
    for (unsigned i = 0; i < NX_PCMON_TABS; i++)
        CHECK(g_nx_pcmon.cycles[0][i] <= 16u);
    {   /* a table arriving in pieces, cut inside a value and inside the head */
        const char *t = table(11, sine11, 1024);
        size_t len = strlen(t);
        char part[64];
        memset(&g_nx_pcmon, 0, sizeof g_nx_pcmon);
        for (size_t o = 0; o < len; o += 37u) {
            size_t n = len - o < 37u ? len - o : 37u;
            memcpy(part, t + o, n);
            part[n] = 0;
            nxw_rx(0, part);
        }
        CHECK_EQ(g_nx_pcmon.cycles[0][0], 11);
    }
    /* the table lines do not disturb the lines after them, nor the frequency mirror: a code of
     * 12 digits (Spooky2 sends 8 decimals) arrives whole */
    nxw_rx(0, ":w20=11,\r\n:w24=846423193440,\r\n");
    CHECK_EQ(g_nx_pcmon.wave[0][0], 11);
    CHECK(g_ch[0].pc_freq[0] == 846423193440ull);
}
