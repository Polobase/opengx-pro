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
static char s_buf[32];

static uint32_t reading_at(unsigned step)   /* flat, a bump of 40 at PEAK1, a plateau of 25 at PEAK2 and the step after */
{
    if (step == PEAK1)
        return 50040u;
    if (step == PEAK2 || step == PEAK2 + 1u)
        return 50025u;
    return 50000u;
}

static const char *gen(unsigned ch, const char *line)
{
    (void)ch;
    if (strncmp(line, ":w24=", 5) == 0 && s_r11_n >= NX_BFB_BASELINE)   /* the sweep: its lines after the baseline */
        s_w24++;
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
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u };
    nxw_clear();
    CHECK_EQ(nx_bfb_start(), NX_OK);
    CHECK_EQ(nx_bfb_start(), NX_BUSY);
    const struct nx_bfb_view *v = nx_bfb_view();
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
    CHECK(run_until_state(v, NX_BFB_SWEEP, 3000));
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
    CHECK_EQ(v->nhits, 2);
    CHECK_EQ(v->hits[0].dev20, 40u * 20u);
    CHECK_EQ(v->hits[1].dev20, 25u * 20u);
    CHECK_EQ(v->hits[0].code, nx_bfb_code(grid_f8(PEAK1 + 1u)));   /* the step after the peak's first reading */
    CHECK_EQ(v->hits[1].code, nx_bfb_code(grid_f8(PEAK2 + 1u)));
    CHECK_EQ(v->missed, 0);
    /* the hits in slot 30, loaded and playing on G1 */
    CHECK(nx_slot_used(30));
    uint64_t hdr[5];
    storage_slot_read(30, 0, hdr, sizeof hdr);
    CHECK(hdr[HDR_WAVE] == 11u && hdr[HDR_AMPL] == 2000u && hdr[HDR_OFFS] == 120u && hdr[HDR_DWELL] == 180u
          && hdr[HDR_COUNT] == 2u);
    CHECK_EQ(g_ch[0].prog_slot, 30);
    CHECK(g_ui_flags & UIF_RUN_G1);
    CHECK_EQ(g_ch[0].prog.freq[0], v->hits[0].code);
    CHECK(strncmp(g_names[30], "Biofeedback G1", 14) == 0);
    run_ms(1000, 1000);
    char l24[32];
    snprintf(l24, sizeof l24, ":w24=%llu,", (unsigned long long)v->hits[0].code);
    CHECK(has(0, l24) && has(0, ":w11=1,1,"));
    nxw_on_line = 0;
}

static void stopped(void)
{
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = s_mute = 0;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 1, 29, 1000u, 60u };
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
    CHECK_EQ(v->state, NX_BFB_IDLE);
    CHECK(!(g_ui_flags & UIF_RUN_G2));
    CHECK(has(1, ":w11=0,0,"));
    CHECK(!nx_slot_used(29));
    /* a generator that answers no read: the scan fails and stops */
    nxw_reset();
    nxw_on_line = gen;
    s_w24 = s_r11_n = 0;
    s_mute = 1;
    nxw_run_until(4600);
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, 30, 2000u, 180u };
    CHECK_EQ(nx_bfb_start(), NX_OK);
    run_ms(8000, 1000);
    CHECK_EQ(v->state, NX_BFB_FAILED);
    CHECK(!(g_ui_flags & UIF_RUN_G1));
    CHECK(has(0, ":w11=0,0,"));
    nxw_on_line = 0;
}
#endif

void test_nx_bfb(void)
{
#if NX_FEAT_BFB
    pure();
    scan();
    stopped();
#endif
}
