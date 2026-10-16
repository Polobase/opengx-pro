/* The biofeedback scan (src/nx/nx_bfb.c): the vendor token against a recorded handshake, the
 * :w24 code, the sweep grid, and a whole scan on the world with a generator model that answers
 * the reads: the handshake lines, the ramp, the baseline, two peaks in the readings found as
 * hits at the frequency of the step after them, the hits stored in the slot and played. */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>

#include "link_internal.h"
#include "nx_bfb.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_slots.h"
#include "nx_src.h"
#include "nx_settings.h"
#include "nx_ui_int.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "storage.h"
#include "test_nx.h"

#if NX_FEAT_BFB
#define PEAK1 5000u   /* sweep steps whose reading stands out */
#define PEAK2 9000u

static unsigned s_w24;       /* :w24 lines of the sweep so far (0: before it) */
static unsigned s_r11_n;     /* :r11 reads answered */
static unsigned s_mute;      /* reads from this count on get no reply (0: all answered) */
static uint64_t s_f8;        /* the frequency of the last :w24 */
static uint64_t s_target;    /* a refining pass: the reading stands out within a step of this frequency (0: by step) */
static char s_buf[32];

static uint64_t t_code_f8(uint64_t code)   /* a :w24 code back to 1e-8 Hz */
{
    uint64_t f = code / 10u;
    for (unsigned e = (unsigned)(code % 10u); e; e--)
        f *= 10u;
    return f;
}

extern const uint16_t k_hk_base[20], k_hk_sweep[15130], k_hk_hit_dev20[10];
extern const uint64_t k_hk_hit_f8[10];
static unsigned s_rec;       /* 1: the readings of the recorded Spooky2 scan (bfb_huntkill_fixture.c) */

static uint32_t reading_at(unsigned step)   /* flat, a bump of 40 at PEAK1, a plateau of 25 at PEAK2 and the step after;
                                             * a startup transient 2 % up at steps 1 to 3 with a bump at 2 (no hit: the
                                             * window settles at step 24 and drops it) */
{
    if (s_rec) {   /* the recording: its baseline's last 20 readings seed the window, then its sweep's */
        if (!step)
            return s_r11_n >= NX_BFB_BASELINE - NX_BFB_WINDOW + 1u ? k_hk_base[s_r11_n - (NX_BFB_BASELINE - NX_BFB_WINDOW + 1u)] : k_hk_base[0];
        return step <= 15130u ? k_hk_sweep[step - 1u] : k_hk_sweep[15129];
    }
    if (s_target) {
        uint64_t d = s_f8 > s_target ? s_f8 - s_target : s_target - s_f8;
        return d * 12000u <= s_target ? 50040u : 50000u;   /* one or two steps of the pass's grid */
    }
    if (step >= 1u && step <= 3u)
        return step == 2u ? 51040u : 51000u;
    if (step == PEAK1)
        return 50040u;
    if (step == PEAK2 || step == PEAK2 + 1u)
        return 50025u;
    return 50000u;
}

static const char *gen(unsigned ch, const char *line)
{
    if (ch == g_nx_bfb_cfg.ch && strncmp(line, ":w24=", 5) == 0) {   /* the scan's steps (the other generator's own lines do not count) */
        s_f8 = t_code_f8(strtoull(line + 5, 0, 10));
        if (s_r11_n >= NX_BFB_BASELINE)   /* the sweep: its lines after the baseline */
            s_w24++;
    }
    if (strcmp(line, ":r90=271543986,") == 0)
        return ":r90=726911191,941378256.\r\n";   /* a recorded handshake: its token is 883542462 */
    if (strcmp(line, ":r11=,") == 0) {
        if (s_mute && s_r11_n >= s_mute)
            return 0;
        s_r11_n++;
        snprintf(s_buf, sizeof s_buf, ":r11=%u.\r\n", (unsigned)reading_at(s_w24));
        return s_buf;
    }
    if (strcmp(line, ":r12=,") == 0)
        return (s_mute && s_r11_n >= s_mute) ? 0 : ":r12=46880.\r\n";
    return 0;
}

/* Lines in the transmit log of ch (since the last nxw_clear) that start with prefix; the position
 * (1-based) of the first one, and how many. */
static unsigned count_prefix(unsigned ch, const char *prefix, unsigned *first)
{
    unsigned n = 0, line = 0;
    size_t k = strlen(prefix);
    for (size_t i = 0; i + k <= tx_len[ch]; i++) {
        if (i != 0 && tx_log[ch][i - 1] != '\n')
            continue;
        line++;
        if (memcmp(tx_log[ch] + i, prefix, k) == 0) {
            if (first && !n)
                *first = line;
            n++;
        }
    }
    return n;
}

static int has(unsigned ch, const char *line)   /* the line (with its "\r\n"): its position, 0 = not sent */
{
    char l[40];
    unsigned first = 0;
    snprintf(l, sizeof l, "%s\r\n", line);
    return count_prefix(ch, l, &first) ? (int)first : 0;
}

static void run_ms(unsigned ms, unsigned pass_us)
{
    for (unsigned i = 0; i < ms * 1000u / pass_us; i++)
        nxw_step(pass_us);
}

static uint64_t grid_f8(unsigned step)   /* the sweep's frequency at step (1-based), as nx_bfb.c advances it */
{
    uint64_t f = NX_BFB_START_F8;
    for (unsigned i = 0; i < step; i++)
        f += f / 4000u;
    return f;
}

static void pure(void)
{
    char tok[10];
    nx_bfb_auth_token("271543986", "941378256", tok);
    CHECK(strcmp(tok, "883542462") == 0);
    CHECK_EQ(nx_bfb_code(4101025000000ull), 41010256u);         /* 41010.25 Hz as Spooky2 sends it */
    CHECK_EQ(nx_bfb_code(NX_BFB_START_F8), 410008u);            /* 41000 Hz */
    CHECK_EQ(nx_bfb_code(179695627039622ull), 1796956270396220ull);   /* 8 decimals: exponent 0 */
    /* the grid: NX_BFB_STEPS steps, the first 41010.25 Hz, the last just past 1.8 MHz */
    uint64_t f = NX_BFB_START_F8;
    unsigned n = 0;
    while (f <= NX_BFB_END_F8) {
        f += f / 4000u;
        n++;
    }
    CHECK_EQ(n, NX_BFB_STEPS);
    CHECK_EQ(grid_f8(1), 4101025000000ull);
    CHECK(f > NX_BFB_END_F8 && f < 180020000000000ull);
    /* the 0.05 % grid of the quicker speeds */
    f = NX_BFB_START_F8;
    n = 0;
    while (f <= NX_BFB_END_F8) {
        f += f / 2000u;
        n++;
    }
    CHECK_EQ(n, NX_BFB_STEPS2);
}

static int run_until_state(const struct nx_bfb_view *v, uint8_t st, unsigned max_ms)
{
    for (unsigned ms = 0; ms < max_ms; ms++) {
        nxw_step(1000);
        if (v->state == st)
            return 1;
    }
    return 0;
}

static void scan(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    nxw_run_until(4600);   /* past the boot gate */
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u, 0, 0 };
    nxw_clear();
    nx_ui_push(&nx_scr_bfb, 0, NX_TR_NONE);   /* the screen is up: the end of the scan takes it to the dashboard */
    CHECK_EQ(nx_bfb_start(), NX_OK);
    CHECK_EQ(nx_bfb_start(), NX_BUSY);
    const struct nx_bfb_view *v = nx_bfb_view();
    CHECK_EQ(v->steps, NX_BFB_STEPS);
    run_ms(100, 1000);
    CHECK(has(0, ":r90=271543986,") && has(0, ":w92=883542462."));   /* the session */
    /* the program: its idle configuration, the sine table, the step at the first ramp value */
    CHECK(run_until_state(v, NX_BFB_RAMP, 3000));
    CHECK_EQ(g_ch[0].prog_slot, NX_SRC_BFB);
    CHECK(g_ui_flags & UIF_RUN_G1);
    unsigned t11 = 0;
    count_prefix(0, ":a11=", &t11);
    int cfg = has(0, ":w28=6,"), on = has(0, ":w11=1,1,"), f0 = has(0, ":w24=410008,");
    CHECK(cfg && t11 && on && f0 && cfg < on && f0 < on && (int)t11 < on);
    /* the ramp: 329 pairs after the first value, up to the target */
    nxw_clear();   /* the log holds 16 KB: cleared between the phases */
    CHECK(run_until_state(v, NX_BFB_BASE, 3000));
    CHECK_EQ(count_prefix(0, ":w28=", 0), NX_BFB_RAMP_STEPS - 1u);
    CHECK_EQ(count_prefix(0, ":w29=", 0), NX_BFB_RAMP_STEPS - 1u);
    CHECK(has(0, ":w28=12,") == 1 && has(0, ":w28=2000,") && has(0, ":w29=2000,"));
    CHECK(!has(0, ":w28=2001,") && !count_prefix(0, ":w24=", 0));
    CHECK_EQ(g_ch[0].prog.hdr[HDR_AMPL], 2000);
    /* the baseline: NX_BFB_BASELINE read pairs, no :w24 before the sweep */
    nxw_clear();
    unsigned r0 = count_prefix(0, ":r11=,", 0);   /* the baseline: a pair per step time, as Spooky2 (14 in a second) */
    run_ms(1000, 1000);
    unsigned r1 = count_prefix(0, ":r11=,", 0);
    CHECK(r1 - r0 >= 13u && r1 - r0 <= 15u);
    CHECK(v->prog >= 13u && v->prog <= 16u);   /* the stage's progress, for the screen's bar */
    CHECK(run_until_state(v, NX_BFB_SWEEP, 20000));
    CHECK_EQ(count_prefix(0, ":r11=,", 0), NX_BFB_BASELINE);
    CHECK_EQ(count_prefix(0, ":r12=,", 0), NX_BFB_BASELINE);
    CHECK_EQ(count_prefix(0, ":w24=", 0), 0);
    CHECK_EQ(has(0, ":r11=,"), 1);
    CHECK_EQ(v->missed, 0);
    CHECK_EQ(v->step, 0);
    /* the sweep: 70 ms per step, a :w24 then the two reads per step, the two peaks found */
    nxw_clear();
    uint32_t t0 = sim_ms();
    run_ms(1000, 1000);
    unsigned steps = v->step;
    CHECK(steps >= 13u && steps <= 15u);
    CHECK(count_prefix(0, ":r11=,", 0) - steps <= 1u);   /* a step may be under way */
    CHECK(count_prefix(0, ":r12=,", 0) - steps <= 1u);
    char lines[64][32];
    unsigned n = nxw_lines(0, lines, 64);
    CHECK(n >= 3u && strncmp(lines[0], ":w24=", 5) == 0);
    CHECK(strcmp(lines[1], ":r11=,") == 0 && strcmp(lines[2], ":r12=,") == 0);
    CHECK(strncmp(lines[3], ":w24=", 5) == 0);
    /* START pauses the sweep: no step goes out, the outputs stay on, the pace excludes the pause;
     * START again goes on */
    nxw_clear();
    nx_bfb_pause(sim_ms());
    unsigned at = v->step;
    uint32_t sm = v->sweep_ms;
    run_ms(1000, 1000);
    CHECK(v->paused);
    CHECK_EQ(v->step, at);
    CHECK(count_prefix(0, ":w24=", 0) <= 1u);   /* at most the line already queued */
    CHECK_EQ(count_prefix(0, ":w11=", 0), 0);
    nx_bfb_pause(sim_ms());
    CHECK(!v->paused);
    CHECK(v->sweep_ms - sm >= 990u && v->sweep_ms - sm <= 1010u);
    run_ms(1000, 1000);
    CHECK(v->step - at >= 13u);
    while (v->state == NX_BFB_SWEEP && sim_ms() - t0 < 1200000u) {
        nxw_clear();
        run_ms(100, 2000);
    }
    CHECK(v->state == NX_BFB_SAVE || v->state == NX_BFB_DONE);
    CHECK_EQ(v->step, NX_BFB_STEPS);
    CHECK(sim_ms() - t0 >= NX_BFB_STEPS * NX_BFB_STEP_MS);
    CHECK(sim_ms() - t0 < NX_BFB_STEPS * (NX_BFB_STEP_MS + 6u));
    run_ms(500, 1000);
    CHECK_EQ(v->state, NX_BFB_DONE);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK_EQ(v->nhits, 2);
    CHECK_EQ(v->hits[0].dev20, 40u * 20u);
    CHECK_EQ(v->hits[1].dev20, 25u * 20u);
    CHECK_EQ(v->hits[0].code, nx_bfb_code(grid_f8(PEAK1)));   /* the peak's first reading's own step */
    CHECK_EQ(v->hits[1].code, nx_bfb_code(grid_f8(PEAK2)));
    CHECK_EQ(v->missed, 0);
    /* the graph: the peak in its column, the trace filled end to end */
    CHECK_EQ(v->hits[0].step, PEAK1);
    CHECK_EQ(v->hits[0].live, PEAK1 - 1u);   /* that reading's place in the live ring: the first sweep reading is 0 */
    unsigned col = PEAK1 * NX_BFB_TRACE / NX_BFB_STEPS;
    CHECK(v->trace[col] >= 39u && v->trace[col] <= 41u);
    CHECK_EQ(v->live_n, NX_BFB_STEPS);   /* every sweep reading went into the live ring */
    CHECK(v->live[(NX_BFB_STEPS - 1u) % NX_BFB_TRACE] <= 1 && v->live[(NX_BFB_STEPS - 1u) % NX_BFB_TRACE] >= -1);
    /* the hits in slot 30, loaded and playing on G1 */
    CHECK(nx_slot_used(30));
    uint64_t hdr[5];
    storage_slot_read(30, 0, hdr, sizeof hdr);
    CHECK(hdr[HDR_WAVE] == 11u && hdr[HDR_AMPL] == 2000u && hdr[HDR_OFFS] == 120u && hdr[HDR_DWELL] == 180u
          && hdr[HDR_COUNT] == 2u);
    CHECK_EQ(g_ch[0].prog_slot, 30);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK(g_ui_flags & UIF_LOOP_G1);   /* the hits play looped (Refine off) */
    CHECK_EQ(g_ch[0].prog.freq[0], v->hits[0].code);
    CHECK(strncmp(g_names[30], "Biofeedback G1", 14) == 0);
    run_ms(1000, 1000);
    char l24[32];
    snprintf(l24, sizeof l24, ":w24=%llu,", (unsigned long long)v->hits[0].code);
    CHECK(has(0, l24) && has(0, ":w11=1,1,"));
    nxw_on_line = 0;
}

/* Spooky2's own scan replayed: the :r11 readings it got on a GX Pro (bfb_huntkill_fixture.c) answer
 * the firmware's reads; the 10 hits Spooky2 showed come out, in its order, with its deviations, at
 * its frequencies (within 1 Hz: the integer grid against its double one) */
static void golden(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    s_rec = 1;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u, 0, 0 };
    CHECK_EQ(nx_bfb_start(), NX_OK);
    const struct nx_bfb_view *v = nx_bfb_view();
    CHECK(run_until_state(v, NX_BFB_SWEEP, 30000));
    /* the other generator plays a two-step program with a 1 s dwell, looped: the runner is hardly
     * ever quiet for 1 s, and the slot write still goes (within SAVE_WAIT_MS) */
    memset(&g_ch[1].prog, 0, sizeof g_ch[1].prog);
    g_ch[1].prog.hdr[HDR_WAVE] = 1;
    g_ch[1].prog.hdr[HDR_AMPL] = 100;
    g_ch[1].prog.hdr[HDR_DWELL] = 1;
    g_ch[1].prog.hdr[HDR_COUNT] = 2;
    g_ch[1].prog.freq[0] = 10008u;   /* 1 kHz */
    g_ch[1].prog.freq[1] = 20008u;
    program_save(5, 1);
    struct nx_action load = { NXA_LOAD, 1, 5, 0, 0 }, play = { NXA_PLAY, 1, 0, 1, 0 };
    CHECK_EQ(nx_core_act(&load), NX_OK);
    CHECK_EQ(nx_core_act(&play), NX_OK);
    CHECK(g_ui_flags & UIF_RUN_G2);
    nxw_clear();
    run_ms(300, 1000);
    CHECK_EQ(count_prefix(0, ":w11=", 0), 0);   /* G2's load does not make the runner resend G1's step into the sweep */
    CHECK(count_prefix(1, ":w24=", 0) >= 1u);
    uint32_t t0 = sim_ms();
    while (v->state == NX_BFB_SWEEP && sim_ms() - t0 < 1200000u) {
        nxw_clear();
        run_ms(100, 2000);
    }
    CHECK(g_ui_flags & UIF_RUN_G2);
    run_ms(3500, 1000);
    CHECK_EQ(v->state, NX_BFB_DONE);
    CHECK(nx_slot_used(30));
    CHECK_EQ(v->step, NX_BFB_STEPS);
    CHECK_EQ(v->missed, 0);
    CHECK_EQ(v->nhits, 10);
    for (unsigned i = 0; i < 10u && i < v->nhits; i++) {
        uint64_t f = t_code_f8(v->hits[i].code), g = k_hk_hit_f8[i], d = f > g ? f - g : g - f;
        CHECK(d <= 100000000ull);
        CHECK_EQ(v->hits[i].dev20, k_hk_hit_dev20[i]);
    }
    s_rec = 0;
    nxw_on_line = 0;
}

/* The fast speed: the 0.05 % grid at 45 ms a step */
static void fast(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u, 2, 0 };
    CHECK_EQ(nx_bfb_start(), NX_OK);
    const struct nx_bfb_view *v = nx_bfb_view();
    CHECK_EQ(v->steps, NX_BFB_STEPS2);
    CHECK(run_until_state(v, NX_BFB_SWEEP, 20000));
    nxw_clear();
    run_ms(1000, 1000);
    CHECK(v->step >= 21u && v->step <= 23u);   /* 45 ms a step */
    char l24[32];
    snprintf(l24, sizeof l24, ":w24=%llu,", (unsigned long long)nx_bfb_code(NX_BFB_START_F8 + NX_BFB_START_F8 / 2000u));
    CHECK_EQ(has(0, l24), 1);   /* the first step is 0.05 % up */
    nx_bfb_stop();
    run_ms(500, 1000);
    CHECK_EQ(v->state, NX_BFB_STOPPED);
    CHECK(v->step >= 21u);   /* what it found stays in the view */
    nxw_on_line = 0;
}

/* The walk of a refining pass: 10 steps each side of a hit, overlapping windows as one, the
 * windows in rising order whatever the hits' order; grid 8000 (a pass after the standard sweep) */
uint16_t nx_bfb_t_walk(const uint64_t *win, unsigned n, uint32_t grid, uint64_t *out, unsigned max);
static int ascending(const uint64_t *f, unsigned n)
{
    for (unsigned i = 1; i < n; i++)
        if (f[i] <= f[i - 1u])
            return 0;
    return 1;
}
static void walk(void)
{
    uint64_t out[64], h = 10000000000000ull, step = h / 8000u;   /* 100 kHz */
    uint64_t one[1] = { h }, near[2] = { h + 8u * step, h }, far[2] = { 2u * h, h }, in[2] = { h + 2u * step, h };
    unsigned n = nx_bfb_t_walk(one, 1, 8000u, out, 64);
    CHECK(n >= 20u && n <= 22u);
    CHECK(ascending(out, n));
    CHECK(out[0] > h - 10u * step && out[0] <= h - 9u * step);   /* the first step above the window's start */
    CHECK(out[n - 1u] >= h + 10u * step);
    n = nx_bfb_t_walk(near, 2, 8000u, out, 64);   /* 8 steps apart: one window, 28 steps wide */
    CHECK(n >= 28u && n <= 31u);
    CHECK(ascending(out, n));
    n = nx_bfb_t_walk(far, 2, 8000u, out, 64);   /* apart: two windows, the lower first */
    CHECK(n >= 40u && n <= 44u);
    CHECK(ascending(out, n));
    CHECK(out[n - 1u] >= 2u * h + 20u * step);
    n = nx_bfb_t_walk(in, 2, 8000u, out, 64);   /* 2 steps apart: the second window adds 2 steps */
    CHECK(n >= 22u && n <= 25u);
    CHECK(ascending(out, n));
}

/* Refine: the hits' program ends by itself, a pass on the hits alone follows (the halved step, 10
 * steps each side of the hit, the baseline at the window's start), its hit replaces the old in
 * the slot and plays; the screen comes up for the pass and leaves for the dashboard after it;
 * with Refine off the program's end leaves the scan as it is */
static void refined(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    s_target = 0;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 30u, 2, 1 };   /* fast, 30 s per hit, refine on */
    CHECK_EQ(nx_bfb_start(), NX_OK);
    const struct nx_bfb_view *v = nx_bfb_view();
    CHECK_EQ(v->pass, 1);
    CHECK(run_until_state(v, NX_BFB_DONE, 400000));   /* the fast sweep reaches PEAK1 only */
    CHECK_EQ(v->nhits, 1);
    CHECK(v->playing && (g_ui_flags & UIF_RUN_G1));
    CHECK(!(g_ui_flags & UIF_LOOP_G1));   /* once: the pass follows its end */
    uint64_t target = t_code_f8(v->hits[0].code);
    s_target = target;
    run_ms(29000, 1000);   /* the program: one hit for 30 s */
    CHECK_EQ(v->state, NX_BFB_DONE);
    CHECK(run_until_state(v, NX_BFB_AUTH, 5000));   /* it ended by itself: the pass */
    nxw_clear();   /* the log filled up during the sweep */
    CHECK_EQ(v->pass, 2);
    CHECK_EQ(v->nhits, 1);   /* the hits and the graph of the pass before stay until its sweep */
    CHECK(v->step > 0u);
    run_ms(300, 1000);
    CHECK(nx_ui_top() == &nx_scr_bfb);
    uint64_t start = target - target * 10u / 4000u;   /* the fast grid halved: f / 4000 a step */
    char l24[32];
    snprintf(l24, sizeof l24, ":w24=%llu,", (unsigned long long)nx_bfb_code(start));
    CHECK(run_until_state(v, NX_BFB_BASE, 12000));
    CHECK(has(0, l24));   /* the baseline at the window's start, not 41 kHz */
    CHECK(run_until_state(v, NX_BFB_SWEEP, 20000));
    CHECK(v->steps >= 19u && v->steps <= 23u);   /* one window, 10 halved steps each side */
    CHECK_EQ(v->nhits, 0);
    CHECK_EQ(v->step, 0);
    nxw_clear();
    run_ms(100, 1000);
    snprintf(l24, sizeof l24, ":w24=%llu,", (unsigned long long)nx_bfb_code(start + start / 4000u));
    CHECK_EQ(has(0, l24), 1);   /* the first step of the window */
    CHECK(run_until_state(v, NX_BFB_DONE, 30000));
    CHECK_EQ(v->nhits, 1);
    uint64_t f = t_code_f8(v->hits[0].code), d = f > target ? f - target : target - f;
    CHECK(d * 2000u <= target);   /* within two steps of the pass's grid */
    CHECK_EQ(g_ch[0].prog_slot, 30);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_ch[0].prog.freq[0], v->hits[0].code);
    CHECK_EQ(g_ch[0].prog.hdr[HDR_COUNT], 1);
    CHECK(v->playing);
    run_ms(300, 1000);
    CHECK(nx_ui_top() == &nx_scr_dash);
    g_nx_bfb_cfg.refine = 0;   /* off: the program's end is the end */
    run_ms(35000, 1000);
    CHECK_EQ(v->state, NX_BFB_DONE);
    CHECK_EQ(v->pass, 2);
    CHECK(!v->playing);
    nxw_on_line = 0;
    s_target = 0;
}

/* START held on the dashboard: the scan screen, on the generator shown; a short START is still
 * the generator's (without a program: its program list) */
static void held(void)
{
    struct nx_uiset u;   /* the first-boot key guide seen, as test_nx_ui's boot */
    nx_uiset_defaults(&u);
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    nxw_reset();
    nxw_run_until(4600);
    CHECK(nx_ui_top() == &nx_scr_dash);
    nx_ui_set_focus(1);
    g_nx_bfb_cfg.ch = 0;
    nxw_press(NX_KEY_START, 700);
    CHECK(nx_ui_top() == &nx_scr_bfb);
    CHECK_EQ(g_nx_bfb_cfg.ch, 1);
    nxw_press(NX_KEY_STOP, 150);
    CHECK(nx_ui_top() == &nx_scr_dash);
    nxw_press(NX_KEY_START, 150);
    CHECK(nx_ui_top() == &nx_scr_lib);
}

static void stopped(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 1, 29, 1000u, 60u, 0, 0 };
    CHECK_EQ(nx_bfb_start(), NX_OK);
    const struct nx_bfb_view *v = nx_bfb_view();
    run_ms(3000, 1000);
    CHECK_EQ(v->ch, 1);
    CHECK(v->state >= NX_BFB_RAMP && v->state <= NX_BFB_SWEEP);
    CHECK(has(1, ":w28=3,"));   /* the first ramp value of 10 V */
    /* STOP ends it: the generator's idle configuration (outputs off) */
    nxw_clear();
    nx_bfb_stop();
    run_ms(500, 1000);
    CHECK_EQ(v->state, NX_BFB_STOPPED);
    CHECK(!(g_ui_flags & UIF_RUN_G2));
    CHECK(has(1, ":w11=0,0,"));
    CHECK_EQ(g_ch[1].prog_slot, 0);   /* the one-step program is unloaded: the dashboard's START scans, not replays */
    CHECK(!nx_slot_used(29));
    /* a generator that answers no read: the scan fails and stops */
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = 0;
    s_mute = 1;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u, 0, 0 };
    CHECK_EQ(nx_bfb_start(), NX_OK);
    run_ms(8000, 1000);
    CHECK_EQ(v->state, NX_BFB_FAILED);
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    CHECK(has(0, ":w11=0,0,"));
    CHECK_EQ(g_ch[0].prog_slot, 0);
    nxw_on_line = 0;
}
#endif

void test_nx_bfb(void)
{
#if NX_FEAT_BFB
    pure();
    walk();
    scan();
    golden();
    fast();
    refined();
    stopped();
    held();
#endif
}
