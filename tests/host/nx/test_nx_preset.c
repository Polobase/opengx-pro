/* Built-in presets with sweeps (src/nx/nx_preset.c, nx_src.c, the runner's setup hook, the
 * queue's stream) with the made-up presets of preset_fixture.c. */
#include "harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_diag.h"
#include "nx_manual.h"
#include "nx_preset.h"
#include "nx_resume.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_txq.h"
#include "nx_uicore.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#if NX_FEAT_PRESET
#define LINES 96
static char s_lines[LINES][32];
static unsigned s_n;

static void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms; i++)
        nxw_step(1000);
}

static void run_to(unsigned ch, uint32_t elapsed)   /* until ch's program clock is there */
{
    while (g_ch[ch].elapsed < elapsed)
        nxw_step(1000);
}

static int act(uint8_t type, uint8_t ch, uint16_t id, uint32_t v)
{
    struct nx_action a = { type, ch, id, v, 0 };
    return nx_core_act(&a);
}

static void snap(unsigned ch)
{
    s_n = nxw_lines(ch, s_lines, LINES);
}

/* The first line at or after `from` that starts with s; -1: none. */
static int find(const char *s, int from)
{
    for (unsigned i = (unsigned)from; i < s_n; i++)
        if (strncmp(s_lines[i], s, strlen(s)) == 0)
            return (int)i;
    return -1;
}

static double hz(const char *line)   /* ":w24=<code>,": Hz = m * 10^(e - 8) */
{
    uint64_t v = strtoull(line + 5, 0, 10);
    return (double)(v / 10u) * pow(10.0, (double)(v % 10u) - 8.0);
}

/* What Spooky2 uploads (read from serial captures). cycles 0: its standard sine, one cycle at
 * full scale, 511 in the middle of the wave; else a preset's: that many cycles at half scale. */
static int sample(unsigned cycles, unsigned i)
{
    if (!cycles)
        return i == 512u ? 511 : (int)floor(512.0 + 511.5 * sin(6.283185307179586 * i / 1024.0));
    unsigned ph = (cycles * i) & 1023u;
    return (int)floor(512.0 + 255.75 * sin(6.283185307179586 * ph / 1024.0));
}

static char s_ref[4200], s_got[4200];

static const char *table_line(unsigned nn, unsigned cycles, int inv)
{
    int n = snprintf(s_ref, sizeof s_ref, ":a%02u=", nn);
    for (unsigned i = 0; i < 1024u; i++)
        n += snprintf(s_ref + n, sizeof s_ref - (size_t)n, "%d,", inv ? 1023 - sample(cycles, i) : sample(cycles, i));
    return s_ref;
}

static void tables(void)
{
    static const uint8_t k_cycles[] = { 1, 2, 3, 5, 8, 11, 16, 255 };
    for (unsigned k = 0; k < sizeof k_cycles; k++) {
        unsigned bad = 0, lo = 1023, hi = 0;
        for (unsigned i = 0; i < 1024u; i++) {
            unsigned v = nx_preset_sample(k_cycles[k], i, 0);
            bad += v != (unsigned)sample(k_cycles[k], i);
            bad += nx_preset_sample(k_cycles[k], i, 1) != 1023u - v;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        CHECK_EQ(bad, 0);
        CHECK(lo == 256u && hi == 767u);
    }
    unsigned bad = 0;   /* the standard sine: full scale */
    for (unsigned i = 0; i < 1024u; i++)
        bad += nx_preset_sample(0, i, 0) != (unsigned)sample(0, i);
    CHECK_EQ(bad, 0);
    CHECK(nx_preset_sample(0, 0, 0) == 512u && nx_preset_sample(0, 256, 0) == 1023u && nx_preset_sample(0, 768, 0) == 0u);
}

static void sources(void)
{
    nxw_reset();
    CHECK_EQ(nx_preset_count(), 2);
    CHECK(strcmp(nx_preset_name(0), "Demo sweep") == 0 && strcmp(nx_preset_name(1), "Demo once") == 0);
    CHECK(strcmp(nx_preset_name(2), "") == 0);
    CHECK(nx_preset_info(0) == (3u | NX_PRESET_LOOP) && nx_preset_info(1) == 2u && nx_preset_info(2) == 0u);
    CHECK(nx_uc_preset_count() == 2u && nx_uc_preset_info(0) == (3u | NX_UC_PRESET_LOOP));
    CHECK(nx_preset_dwell_s(0, 0) == 10u && nx_preset_dwell_s(0, 1) == 4u && nx_preset_dwell_s(0, 2) == 5u);
    CHECK(nx_preset_dwell_s(0, 3) == 1u && nx_preset_dwell_s(9, 0) == 1u);

    /* the program: Out1's table, amplitude and offset, the wire code of each step's start
     * (frequency / cycles, rounded to 1e-8 Hz, trailing zeros in the exponent digit) */
    struct channel *c = &g_ch[0];
    CHECK_EQ(nx_src_fill(NX_SRC_PRESET, 0, c), NX_OK);
    CHECK(c->prog.hdr[HDR_WAVE] == 11u && c->prog.hdr[HDR_AMPL] == 1000u && c->prog.hdr[HDR_OFFS] == 20u);
    CHECK(c->prog.hdr[HDR_DWELL] == 10u && c->prog.hdr[HDR_COUNT] == 3u);
    CHECK_EQ(c->prog.freq[0], 19);      /* 110 Hz / 11 = 10 Hz */
    CHECK_EQ(c->prog.freq[1], 458);     /* 495 Hz / 11 = 45 Hz */
    CHECK_EQ(c->prog.freq[2], 209);     /* 2200 Hz / 11 = 200 Hz */
    CHECK_EQ(c->prog.freq[3], 0);
    CHECK_EQ(nx_src_fill(NX_SRC_PRESET + 1u, 0, c), NX_OK);
    CHECK(c->prog.freq[0] == 4113333330ull);    /* 12.34 Hz / 3 = 4.11333333 Hz */
    CHECK_EQ(c->prog.freq[1], 109);     /* 300 Hz / 3 */
    CHECK_EQ(nx_src_fill(NX_SRC_PRESET + 2u, 0, c), NX_EINVAL);
    CHECK(nx_src_is_preset(NX_SRC_PRESET) && nx_src_is_preset(NX_SRC_PRESET + NX_SRC_PRESETS - 1u));
    CHECK(!nx_src_is_preset(NX_SRC_MANUAL) && !nx_src_is_preset(PROGRAM_SLOTS) && !nx_src_is_set(NX_SRC_PRESET));
    CHECK(nx_src_timed(NX_SRC_PRESET) && nx_src_timed(NX_SRC_SET) && !nx_src_timed(1) && !nx_src_timed(NX_SRC_MANUAL));
    CHECK_EQ(nx_src_dwell_s(NX_SRC_PRESET, 2), 5);
    char name[NX_UC_NAME_MAX + 1u];
    CHECK_EQ(nx_src_name(NX_SRC_PRESET + 1u, name), 9);
    CHECK(strcmp(name, "Demo once") == 0);
}

static void playing(void)
{
    struct nx_vm vm;
    struct nx_run_times t;
    nxw_reset();
    nxw_run_until(4600);   /* past the boot gate */
    nxw_clear();

    /* load and play: the idle configuration, the two wave tables, the Out2 lines, then the step.
     * The program's clock waits for all of it: the first sweep starts at its start. */
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(200);
    CHECK(!nx_txq_idle(0));             /* the first table is on its way */
    CHECK_EQ(g_ch[0].elapsed, 0);
    CHECK(g_ui_flags & UIF_RUN_G1);
    run_ms(1500);
    snap(0);
    static const char *const k_idle[10] = { ":w24=19,", ":w11=0,0,", ":w14=1,", ":w17=0,1,", ":w20=11,", ":w21=11,",
                                            ":w28=1000,", ":w29=1000,", ":w32=20,", ":w33=20," };
    for (unsigned i = 0; i < 10u; i++)
        CHECK(strcmp(s_lines[i], k_idle[i]) == 0);
    CHECK(strncmp(s_lines[10], ":a11=512,529,", 13) == 0);
    CHECK(strncmp(s_lines[11], ":a25=511,494,", 13) == 0);
    CHECK(strcmp(s_lines[12], ":w17=0,0,") == 0 && strcmp(s_lines[13], ":w21=25,") == 0 && strcmp(s_lines[14], ":w33=220,") == 0);
    CHECK(strcmp(s_lines[15], ":w24=19,") == 0 && strcmp(s_lines[16], ":w11=1,1,") == 0 && strcmp(s_lines[17], ":w12=0,0,") == 0);
    /* the tables are Spooky2's, byte for byte: 11 sine cycles at half scale, and its inverse */
    CHECK_EQ(nxw_line(0, 10, s_got, sizeof s_got), 4101);
    CHECK(strcmp(s_got, table_line(11, 11, 0)) == 0);
    CHECK_EQ(nxw_line(0, 11, s_got, sizeof s_got), 4101);
    CHECK(strcmp(s_got, table_line(25, 11, 1)) == 0);
    /* each table is answered before the next line: 300 ms after it in this model */
    CHECK(nxw_line_us[0][11] >= nxw_line_end_us[0][10] + 300000u);
    CHECK(nxw_line_us[0][12] >= nxw_line_end_us[0][11] + 300000u);
    CHECK_EQ(nx_txq_stats(0)->timeouts, 0);
    CHECK(g_ch[0].elapsed > 0u && g_ch[0].elapsed < 400u);   /* the clock runs since the step went out */
    CHECK_EQ(nxw_line_n[1], 10);        /* G2: its idle configuration again, as after every load */

    /* the sweep 110 -> 1210 Hz in 10 s: a :w24 line about every 125 ms, the frequency / 11 */
    for (int j = 18; j < (int)s_n; j++)
        CHECK(strncmp(s_lines[j], ":w24=", 5) == 0);
    uint32_t el = g_ch[0].elapsed;
    nxw_clear();
    run_ms(1000);
    snap(0);
    CHECK(s_n >= 7u && s_n <= 9u);
    double prev = 0;
    for (unsigned i = 0; i < s_n; i++) {
        CHECK(strncmp(s_lines[i], ":w24=", 5) == 0);
        double f = hz(s_lines[i]) * 11.0, lo = 110.0 + 110.0 * el / 1000.0, hi = 110.0 + 110.0 * (el + 1010u) / 1000.0;
        CHECK(f > prev && f >= lo && f <= hi);
        prev = f;
    }
    nx_runner_times(0, &t);
    CHECK(t.step == 1u && t.count == 3u && t.dwell_ms == 10000u && t.total_ms == 19000u);
    /* the dashboard shows the output frequency (not the wire's) and the next step's start */
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].slot, NX_SRC_PRESET);
    CHECK(vm.g[0].freq % 10u == 6u && vm.g[0].freq / 10u > 11000u + 110u * (g_ch[0].elapsed - 130u) / 10u
          && vm.g[0].freq / 10u <= 11000u + 110u * g_ch[0].elapsed / 10u);
    CHECK_EQ(vm.g[0].next, 495006);
    CHECK(vm.g[0].wave == 11u && vm.g[0].ampl_cv == 1000u && vm.g[0].count == 3u && vm.g[0].loop);

    /* the hold at 10 s: its start line, then nothing */
    run_to(0, 9990);
    nxw_clear();
    run_ms(160);
    snap(0);
    int at = find(":w24=458,", 0);
    CHECK(at >= 0 && at <= 1 && (int)s_n == at + 3);
    CHECK(strcmp(s_lines[at + 1], ":w11=1,1,") == 0 && strcmp(s_lines[at + 2], ":w12=0,0,") == 0);
    nxw_clear();
    run_ms(3000);
    CHECK_EQ(tx_len[0], 0);
    nx_uc_vm(&vm);
    CHECK(vm.g[0].freq == 495006u && vm.g[0].next == 2200006u && vm.g[0].step == 2u);

    /* the sweep downwards from 14 s: 2200 -> 550 Hz in 5 s */
    run_to(0, 14050);
    snap(0);
    CHECK(s_n >= 3u && strcmp(s_lines[0], ":w24=209,") == 0);   /* a new step: exactly its start */
    nxw_clear();
    run_ms(1000);
    snap(0);
    CHECK(s_n >= 7u && s_n <= 9u);
    prev = 1e9;
    for (unsigned i = 0; i < s_n; i++) {
        double f = hz(s_lines[i]) * 11.0;
        CHECK(f < prev && f < 2200.0 && f > 2200.0 - 330.0 * 1.3);
        prev = f;
    }
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].next, 110006);   /* looping: the first step again */

    /* a pause in the sweep: the idle configuration (outputs off; it sets Out2 to Out1's table and
     * offset). Play again: the Out2 lines, then the step where the sweep is now; no tables. */
    nxw_clear();
    CHECK_EQ(act(NXA_PAUSE, 0, 0, 1), NX_OK);
    run_ms(200);
    snap(0);
    CHECK_EQ(s_n, 10);
    CHECK(strcmp(s_lines[1], ":w11=0,0,") == 0 && strcmp(s_lines[5], ":w21=11,") == 0 && strcmp(s_lines[9], ":w33=20,") == 0);
    el = g_ch[0].elapsed;
    run_ms(500);
    CHECK_EQ(g_ch[0].elapsed, el);
    nxw_clear();
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(100);
    snap(0);
    CHECK(s_n >= 6u && find(":a", 0) < 0);
    CHECK(strcmp(s_lines[0], ":w17=0,0,") == 0 && strcmp(s_lines[1], ":w21=25,") == 0 && strcmp(s_lines[2], ":w33=220,") == 0);
    CHECK(strncmp(s_lines[3], ":w24=", 5) == 0 && strcmp(s_lines[3], ":w24=209,") != 0 && strcmp(s_lines[4], ":w11=1,1,") == 0);
    double want = 2200.0 - 330.0 * (el - 14000u) / 1000.0;
    CHECK(fabs(hz(s_lines[3]) * 11.0 - want) < 15.0);
    CHECK(g_ch[0].elapsed - el < 100u);   /* the clock waited for the Out2 lines */

    /* a load on the other generator sends G1's step again (as the original does): its sweep goes on */
    run_ms(300);
    nxw_clear();
    CHECK_EQ(act(NXA_LOAD, 1, NX_SRC_PRESET + 1u, 0), NX_OK);
    run_ms(60);
    snap(0);
    CHECK(s_n >= 3u && strncmp(s_lines[0], ":w24=", 5) == 0 && strcmp(s_lines[0], ":w24=209,") != 0);
    CHECK(strcmp(s_lines[1], ":w11=1,1,") == 0 && find(":w17", 0) < 0 && find(":a", 0) < 0);
    snap(1);                            /* G2: loaded, not playing: its idle configuration, no tables */
    CHECK(s_n == 10u && strcmp(s_lines[0], ":w24=4113333330,") == 0);

    /* the end of the list: looping, the first step again, from its start */
    run_to(0, 18990);
    run_ms(300);
    CHECK(g_ui_flags & UIF_RUN_G1);
    snap(0);
    CHECK(find(":w24=19,", 0) >= 0);
    nx_runner_times(0, &t);
    CHECK(t.step == 1u && t.elapsed_ms < 400u);

    /* the resume snapshot keeps it like a built-in set */
    struct nx_resume_rec r;
    nx_resume_capture(&r);
    CHECK(r.g[0].slot == NX_SRC_PRESET && (r.g[0].flags & NX_RES_RUN) && (r.g[0].flags & NX_RES_LOOP));
    CHECK(r.g[0].chain == 0u && r.g[0].elapsed_ms == g_ch[0].elapsed);

    /* stop: the idle configuration of step 1. A new load uploads the tables again. */
    nxw_clear();
    CHECK_EQ(act(NXA_STOP, 0, 0, 0), NX_OK);
    run_ms(200);
    snap(0);
    CHECK(s_n == 10u && strcmp(s_lines[0], ":w24=19,") == 0 && strcmp(s_lines[1], ":w11=0,0,") == 0);
    nxw_clear();
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(1700);
    snap(0);
    CHECK(find(":a11=", 0) == 10 && find(":a25=", 0) == 11 && find(":w24=19,", 12) == 15);

    /* the other preset on G2: 3 cycles per table, once: it ends as a program does */
    nxw_clear();
    CHECK_EQ(act(NXA_PLAY, 1, 0, 0), NX_OK);
    run_ms(1700);
    snap(1);
    CHECK(find(":a11=", 0) == 0 && find(":a25=", 0) == 1);
    CHECK_EQ(nxw_line(1, 0, s_got, sizeof s_got), 4101);
    CHECK(strcmp(s_got, table_line(11, 3, 0)) == 0);
    CHECK(find(":w24=4113333330,", 2) == 5);
    run_ms(2000);
    snap(1);
    CHECK(find(":w24=109,", 6) > 0);    /* 300 Hz / 3 */
    CHECK(g_ui_flags & UIF_RUN_G2);
    nxw_clear();
    run_ms(3100);
    snap(1);
    CHECK(!(g_ui_flags & UIF_RUN_G2));
    CHECK(find(":w11=0,0,", 0) >= 0);
    CHECK(g_ui_flags & UIF_RUN_G1);     /* G1 plays on */
}

/* The preset leaves 11 sine cycles in the generator's table 11, and nx does not know what a PC
 * left there: the manual generator's sine gets Spooky2's standard sine first, once. */
static void plain_sine(void)
{
#if NX_FEAT_MANUAL
    struct nx_manual *m = &g_nx_manual;
    nxw_reset();
    nxw_run_until(4600);
    *m = (struct nx_manual){ NX_MANUAL_VER, NX_MAN_FIXED, 2, 0, NX_SW_LOOP, 0, 50, 100000u, 10000u, 110000u, 10u };
    /* a square (table 12): as before, no table */
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(300);
    snap(0);
    CHECK(find(":a", 0) < 0 && find(":w20=12,", 0) > 0 && find(":w11=1,1,", 0) > 0);
    /* the sine after power-on: the standard sine into table 11, then the step; the clock waits */
    m->wave = 1;
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(1200);
    snap(0);
    CHECK(find(":w20=11,", 0) == 4 && find(":a11=512,515,518,", 0) == 10 && find(":a25=", 0) < 0);
    CHECK(find(":w24=1000006,", 11) == 11 && find(":w11=1,1,", 11) == 12);
    CHECK_EQ(nxw_line(0, 10, s_got, sizeof s_got), 3930);
    CHECK(strcmp(s_got, table_line(11, 0, 0)) == 0);
    CHECK(g_ch[0].elapsed > 0u && g_ch[0].elapsed < 600u);
    /* again: the table is there */
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(300);
    snap(0);
    CHECK(find(":a", 0) < 0 && find(":w11=1,1,", 0) > 0);
    /* G2 has its own generator: its table once */
    nxw_clear();
    CHECK_EQ(nx_manual_play(1), NX_OK);
    run_ms(1200);
    snap(1);
    CHECK(find(":a11=", 0) == 10 && find(":w11=1,1,", 11) == 12);

    /* a preset on G1 puts its 11 cycles into table 11; the manual sine after it gets the standard
     * sine back, the preset after that its own again */
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(2500);
    CHECK(g_ch[0].elapsed > 0u);
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(1200);
    snap(0);
    CHECK(find(":a11=512,515,518,", 0) == 10 && find(":w11=1,1,", 11) == 12);
    nxw_clear();
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(1700);
    snap(0);
    CHECK(find(":a11=512,529,546,", 0) == 10 && find(":a25=", 0) == 11);

    /* a PC session: Spooky2 may have put anything there */
    CHECK_EQ(act(NXA_STOP, 0, 0, 0), NX_OK);
    run_ms(200);
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(1200);                       /* the standard sine is back */
    nxw_pb1 = 1;
    run_ms(300);
    CHECK_EQ(g_ui_state, ST_PC);
    nxw_pb1 = 0;
    run_ms(300);
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(1200);
    snap(0);
    CHECK(find(":a11=512,515,518,", 0) >= 0 && find(":w11=1,1,", 0) > 0);

    /* a generator that does not answer the table: the sine plays all the same after the wait, and
     * the table is not sent again */
    nxw_reset();
    nxw_run_until(4600);
    *m = (struct nx_manual){ NX_MANUAL_VER, NX_MAN_FIXED, 1, 0, NX_SW_LOOP, 0, 50, 100000u, 10000u, 110000u, 10u };
    nxw_gen(0, NXW_NOTABLE, 1000);
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(3000);
    snap(0);
    CHECK(find(":a11=", 0) == 10 && find(":w11=1,1,", 11) == 12);
    CHECK(g_ui_flags & UIF_RUN_G1);
    nxw_clear();
    CHECK_EQ(nx_manual_play(0), NX_OK);
    run_ms(300);
    snap(0);
    CHECK(find(":a", 0) < 0 && find(":w11=1,1,", 0) > 0);

    /* a stored slot plays its own table: nothing is added to its lines */
    nxw_reset();
    nxw_run_until(4600);
    g_ch[0].prog.hdr[HDR_WAVE] = 11;    /* even one that names table 11 */
    g_ch[0].prog.freq[0] = 7278;
    g_ch[0].prog_slot = 3;
    nxw_clear();
    CHECK_EQ(act(NXA_PLAY, 0, 0, 0), NX_OK);
    run_ms(300);
    snap(0);
    CHECK(find(":a", 0) < 0 && find(":w11=1,1,", 0) >= 0);
#endif
}

static void failures(void)
{
    /* a generator that does not answer the table: the preset stops, the second table is not sent */
    nxw_reset();
    nxw_run_until(4600);
    nxw_gen(0, NXW_NOTABLE, 1000);
    nxw_clear();
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(1500);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_ch[0].elapsed, 0);
    run_ms(1500);                       /* 0.36 s of table and NX_TXQ_STREAM_ACK_MS */
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    CHECK_EQ(g_ch[0].elapsed, 1);       /* stopped */
    CHECK_EQ(nx_txq_stats(0)->timeouts, 1);
    run_ms(500);
    snap(0);
    CHECK(find(":a11=", 0) == 10 && find(":a25=", 0) < 0 && find(":w11=1,1,", 0) < 0);
    /* play again: the table again (the generator answers now) */
    nxw_gen(0, NXW_ACK, 1000);
    nxw_clear();
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(1700);
    snap(0);
    CHECK(find(":a11=", 0) >= 0 && find(":a25=", 0) > 0 && find(":w11=1,1,", 0) > 0);
    CHECK(g_ui_flags & UIF_RUN_G1);

    /* the PC connects while a table is on its way: the line is finished, the next one is not
     * begun, then PC mode starts as always; nothing is sent in PC mode */
    nxw_reset();
    nxw_run_until(4600);
    nxw_clear();
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_OK);
    CHECK_EQ(act(NXA_PLAY, 0, 0, 1), NX_OK);
    run_ms(200);
    nxw_pb1 = 1;
    run_ms(1200);
    CHECK_EQ(g_ui_state, ST_PC);
    CHECK_EQ(g_ch[0].prog_slot, 0);
    snap(0);
    CHECK(find(":a11=", 0) == 10 && find(":a25=", 0) < 0);
    CHECK_EQ(nxw_line(0, 10, s_got, sizeof s_got), 4101);
    CHECK(strcmp(s_lines[11], ":w11=0,0,") == 0);   /* the PC-mode entry's own line */
    CHECK_EQ(act(NXA_LOAD, 0, NX_SRC_PRESET, 0), NX_REFUSED_PC);
    nxw_clear();
    run_ms(1000);
    CHECK_EQ(tx_len[0] + tx_len[1], 0);
    CHECK_EQ(g_nx_diag.txq_rejected_pc, 0);
}
#endif

void test_nx_preset(void)
{
#if NX_FEAT_PRESET
    tables();
    sources();
    playing();
    plain_sine();
    failures();
#endif
}
