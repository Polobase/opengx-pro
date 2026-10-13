/* The manual generator (src/nx/nx_manual.c): the sweep's frequencies, the lines of playing, of a
 * live frequency change and of a waveform/amplitude change, a sweep that runs once. */
#include "harness.h"

#include <stdlib.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_manual.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#if NX_FEAT_MANUAL
static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms; i++)
        nxw_step(1000);
}

static int has(unsigned ch, const char *line)
{
    char lines[64][32];
    unsigned n = nxw_lines(ch, lines, 64);
    for (unsigned i = 0; i < n; i++)
        if (strcmp(lines[i], line) == 0)
            return (int)i + 1;
    return 0;
}

static void sweeps(void)
{
    struct nx_manual *m = &g_nx_manual;
    *m = (struct nx_manual){ NX_MANUAL_VER, NX_MAN_SWEEP, 1, 0, NX_SW_LOOP, 0, 50, 100000u, 10000u, 110000u, 10u };
    CHECK_EQ(nx_manual_chz(0), 10000);
    CHECK_EQ(nx_manual_chz(5000), 60000);
    CHECK_EQ(nx_manual_chz(10000), 10000);   /* again from the start */
    m->repeat = NX_SW_PINGPONG;
    CHECK_EQ(nx_manual_chz(15000), 60000);
    CHECK_EQ(nx_manual_chz(19000), 20000);   /* on the way back */
    m->repeat = NX_SW_ONCE;
    CHECK_EQ(nx_manual_chz(12000), 110000);
    m->shape = 1;   /* logarithmic 100 Hz -> 10 kHz: 1 kHz half way, 316.23 Hz at a quarter */
    m->end_chz = 1000000u;
    uint32_t f = nx_manual_chz(5000);
    CHECK(f > 99900u && f < 100100u);
    f = nx_manual_chz(2500);
    CHECK(f > 31590u && f < 31660u);
    CHECK_EQ(nx_manual_chz(0), 10000);
    m->start_chz = 1000000u;   /* downwards */
    m->end_chz = 10000u;
    f = nx_manual_chz(5000);
    CHECK(f > 99900u && f < 100100u);
    m->mode = NX_MAN_FIXED;
    CHECK_EQ(nx_manual_chz(7000), 100000);
}

static void playing(void)
{
    nxw_reset();
    nxw_run_until(4600);   /* past the boot gate */
    struct nx_manual *m = &g_nx_manual;
    *m = (struct nx_manual){ NX_MANUAL_VER, NX_MAN_FIXED, 1, 0, NX_SW_LOOP, 0, 50, 100000u, 10000u, 110000u, 10u };
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    CHECK_EQ(g_ch[0].prog_slot, NX_SRC_MANUAL);
    run_ms(300);
    CHECK(has(0, ":w20=11,") && has(0, ":w28=500,") && has(0, ":w24=1000006,") && !has(0, ":w11=1,1,"));
    run_ms(900);   /* the sine after power-on: Spooky2's standard sine into table 11 first (nx_preset.h) */
    CHECK(has(0, ":w11=1,1,"));
    run_ms(3000);
    CHECK(g_ui_flags & UIF_RUN_G1);   /* a fixed frequency has no end */
    struct nx_run_times t;
    nx_runner_times(0, &t);
    CHECK(t.count == 1u && t.step == 1u && t.total_ms == 0u && t.step_left_ms == UINT32_MAX);

    /* a frequency while it plays: one :w24 line */
    nxw_clear();
    m->f_chz = 200000u;
    nx_manual_changed();
    run_ms(200);
    char lines[64][32];
    CHECK_EQ(nxw_lines(0, lines, 64), 1);
    CHECK(strcmp(lines[0], ":w24=2000006,") == 0);

    /* an amplitude: outputs off, the idle configuration with it, on again */
    nxw_clear();
    m->ampl_dv = 60;
    nx_manual_changed();
    run_ms(300);
    int off = has(0, ":w11=0,0,"), amp = has(0, ":w28=600,"), on = has(0, ":w11=1,1,");
    CHECK(off && amp && on && off < amp && amp < on);
    CHECK(g_ui_flags & UIF_RUN_G1);

    /* a sweep 100 -> 1100 Hz in 10 s, again and again: a :w24 line every 125 ms, rising */
    m->mode = NX_MAN_SWEEP;
    nx_manual_changed();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(100);
    nxw_clear();
    run_ms(1000);
    unsigned n = nxw_lines(0, lines, 64), k = 0;
    uint64_t prev = 0;
    for (unsigned i = 0; i < n; i++) {
        CHECK(strncmp(lines[i], ":w24=", 5) == 0);
        uint64_t v = strtoull(lines[i] + 5, 0, 10);
        CHECK(v > prev);
        prev = v;
        k++;
    }
    CHECK(k >= 7u && k <= 9u);
    CHECK(prev > 190006u && prev < 220006u);   /* about 1.1 s in: about 210 Hz */
    nx_runner_times(0, &t);
    CHECK_EQ(t.total_ms, 10000);

    /* once: it ends as a program does */
    m->repeat = NX_SW_ONCE;
    nx_manual_changed();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(9000);
    CHECK(g_ui_flags & UIF_RUN_G1);
    nxw_clear();
    run_ms(1500);
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    CHECK(has(0, ":w11=0,0,"));
}
#endif

void test_nx_manual(void)
{
#if NX_FEAT_MANUAL
    sweeps();
    playing();
#endif
}
