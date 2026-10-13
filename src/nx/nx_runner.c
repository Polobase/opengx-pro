/* nx program runner (nx_runner.h): src/classic/runner.c's decisions, per link, into nx_txq. */
#include "nx_runner.h"

#include "nx_config.h"
#include "nx_chain.h"
#include "nx_core.h"
#include "nx_hist.h"
#include "nx_manual.h"
#include "nx_preset.h"
#include "nx_src.h"
#include "nx_terrain.h"
#include "nx_timer.h"
#include "nx_txq.h"
#include "nx_ui.h"
#include "nx_vm.h"
#include "runner_seq.h"
#include "state.h"

/* DEVIATION: a count above 200 (only possible through a bad upload) ends after step 200; the
 * original plays the bytes behind the frequency table as steps 201..255. */
static uint32_t step_count(const struct channel *c)
{
    uint64_t n = c->prog.hdr[HDR_COUNT];
    return n > PROGRAM_STEPS ? PROGRAM_STEPS : (uint32_t)n;
}

#define NX_TIMED (NX_FEAT_TERRAIN || NX_FEAT_PRESET)

#if NX_TIMED
/* A built-in set's or preset's steps have their own dwells (nx_src.h): the step at elapsed is
 * found from the one before, as the runner asks on every pass; t0..t1 is its time. */
struct at {
    uint32_t t0, t1, total;
    uint16_t step;
    uint8_t id;
};
static struct at s_at[2];

static uint32_t dwell_ms(unsigned id, unsigned i)
{
    uint32_t s = nx_src_dwell_s(id, i);
    return s > 86400u ? 86400u * NX_DWELL_UNITS : s * NX_DWELL_UNITS;
}

static const struct at *set_at(unsigned ch, const struct channel *c)
{
    struct at *a = &s_at[ch];
    uint32_t n = step_count(c), el = c->elapsed;
    if (a->id != c->prog_slot) {
        a->id = c->prog_slot;
        a->total = 0;
        for (unsigned i = 0; i < n; i++)
            a->total += dwell_ms(a->id, i);
        a->step = 0;
    }
    if (!a->step || el < a->t0) {
        a->step = 1;
        a->t0 = 0;
        a->t1 = dwell_ms(a->id, 0);
    }
    while (el >= a->t1 && a->step <= n) {
        a->t0 = a->t1;
        a->step++;
        a->t1 = a->step <= n ? a->t0 + dwell_ms(a->id, a->step - 1u) : UINT32_MAX;
    }
    return a;
}
#endif

static uint32_t current_step(unsigned ch, const struct channel *c)
{
#if NX_FEAT_MANUAL
    if (c->prog_slot == NX_SRC_MANUAL)
        return nx_manual_step(c);   /* one step without an end (a sweep once: then the end) */
#endif
#if NX_TIMED
    if (nx_src_timed(c->prog_slot))
        return set_at(ch, c)->step;
#else
    (void)ch;
#endif
    uint64_t count = c->prog.hdr[HDR_COUNT], dwell = c->prog.hdr[HDR_DWELL];
    if (count == 0)
        return 0;
    if (dwell == 0)   /* as src/classic/runner.c: a dwell of 0 counts as 1 s */
        dwell = 1;
    if (dwell > UINT64_MAX / NX_DWELL_UNITS)
        return 1;
    return (uint32_t)(c->elapsed / (dwell * NX_DWELL_UNITS) + 1u);
}

static uint8_t s_cfg_due;   /* bit ch: a program was loaded and its idle configuration has not gone out */

void nx_runner_loaded(unsigned ch)
{
    ch &= 1u;
    s_cfg_due = (uint8_t)(s_cfg_due | (1u << ch));
#if NX_FEAT_PRESET
    nx_preset_loaded(ch);
#endif
}

static void play_step(unsigned ch, struct channel *c, uint32_t step)
{
#if NX_FEAT_PRESET
    int again = c->step_sent == 0xFFu;
#endif
    c->step_freq = c->prog.freq[step - 1u];
    c->step_sent = (uint8_t)step;
    /* The original zeroes a missing (0xFFFF) gating pair in RAM, where a later :p without :g
     * saves it: kept, because the upload buffer is PC-visible. */
    uint16_t *pair = &c->params[2u * step - 2u];
    if (pair[0] == 0xFFFFu)
        pair[0] = 0;
    if (pair[1] == 0xFFFFu)
        pair[1] = 0;
    struct seq s;
    /* Loaded and played in one go (a chain's next program, a Terrain set, the manual generator):
     * the new program's idle configuration (its waveform table and amplitude) goes out first, as
     * it does when the original's START comes after the load; the step follows on the next pass. */
    int cfg = (s_cfg_due >> ch) & 1u;
    int idle = !(g_ui_flags & UIF_RUN(ch)) || cfg;
#if NX_FEAT_PRESET
    if (!idle) {
        if (!nx_preset_setup(ch)) {   /* a preset: its wave tables, then its Out2 lines; the step waits */
            c->step_sent = 0xFF;
            return;
        }
        uint64_t code = again ? nx_preset_code(ch) : 0u;
        if (code)   /* not a new step (a pause, a load on the other generator): where its sweep is now */
            c->step_freq = code;
    }
#endif
    if (idle)
        seq_idle(&s, c->step_freq, &c->prog);
    else
        seq_play(&s, c->step_freq, pair[0], pair[1]);
    if (nx_txq_submit_seq(ch, &s) != NX_OK) {
        c->step_sent = 0xFF;   /* not queued: try again next pass */
        return;
    }
    s_cfg_due = (uint8_t)(s_cfg_due & ~(1u << ch));
    if (cfg && (g_ui_flags & UIF_RUN(ch)))
        c->step_sent = 0xFF;   /* now the step */
}

static void finished(unsigned ch, struct channel *c)
{
    if (g_ui_flags & UIF_RUN(ch))   /* the UI's FINISHED latch, or a loop pass */
        nx_ui_post(NXE_FINISHED, (uint8_t)ch, (g_ui_flags & UIF_LOOP(ch)) ? 1u : 0u);
    if (!(g_ui_flags & UIF_LOOP(ch))) {
        if (g_ui_flags & UIF_RUN(ch)) {
            nx_timer_finished(ch);
            nx_chain_finished(ch);
            nx_terrain_finished(ch);
            nx_hist_end(ch, NX_HEND_FINISHED);
            struct seq s;
            seq_off(&s);
            nx_txq_submit_seq(ch, &s);
        }
        flags_clear(&g_ui_flags, UIF_RUN(ch));
    }
    c->elapsed = 1;
}

void nx_runner_task(void)
{
    if (g_ui_state == ST_PC || g_ui_state == ST_CAL || nx_core_runner_held())
        return;
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (!nx_txq_idle(ch))   /* a sequence is atomic per link, as in the original */
            continue;
        struct channel *c = &g_ch[ch];
        uint32_t step = current_step(ch, c);
        c->step = (uint8_t)(step > 255u ? 255u : step);
        if (step > step_count(c))
            finished(ch, c);
        else if (step != c->step_sent && step != 0)
            play_step(ch, c, step);
    }
}

void nx_runner_tick_1ms(void)
{
#if NX_FEAT_PRESET
    unsigned hold = g_nx_preset_hold;
#else
    unsigned hold = 0;
#endif
    if ((g_ui_flags & UIF_RUN_G1) && !(hold & 1u))
        g_ch[0].elapsed++;
    if ((g_ui_flags & UIF_RUN_G2) && !(hold & 2u))
        g_ch[1].elapsed++;
}

void nx_runner_times(unsigned ch, struct nx_run_times *t)
{
    ch &= 1u;
    const struct channel *c = &g_ch[ch];
    uint64_t dwell = c->prog.hdr[HDR_DWELL];
    uint32_t count = step_count(c);
#if NX_FEAT_MANUAL
    if (c->prog_slot == NX_SRC_MANUAL) {   /* a sweep's time; a fixed frequency has none */
        uint32_t T = nx_manual_period_ms(), el = c->elapsed;
        if (T && g_nx_manual.repeat != NX_SW_ONCE)
            el %= T;
        t->count = t->step = 1;
        t->dwell_ms = t->total_ms = T;
        t->elapsed_ms = el > T ? T : el;
        t->left_ms = T - t->elapsed_ms;
        t->step_left_ms = UINT32_MAX;   /* no step ends: never in the way of a flash write */
        return;
    }
#endif
#if NX_TIMED
    if (nx_src_timed(c->prog_slot) && count) {
        const struct at *a = set_at(ch, c);
        t->count = count;
        t->total_ms = a->total;
        t->elapsed_ms = c->elapsed > a->total ? a->total : c->elapsed;
        t->left_ms = a->total - t->elapsed_ms;
        if (a->step > count) {   /* at the very end */
            t->step = count;
            t->dwell_ms = dwell_ms(a->id, count - 1u);
            t->step_left_ms = 0;
        } else {
            t->step = a->step;
            t->dwell_ms = a->t1 - a->t0;
            t->step_left_ms = a->t1 - t->elapsed_ms;
        }
        return;
    }
#endif
    if (dwell == 0)
        dwell = 1;
    if (dwell > 86400u)   /* a day per step at most: the step time stays within 32 bits */
        dwell = 86400u;
    t->count = count;
    t->dwell_ms = (uint32_t)dwell * NX_DWELL_UNITS;
    uint64_t total = (uint64_t)t->dwell_ms * count;
    t->total_ms = total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
    uint32_t el = c->elapsed;
    if (el > t->total_ms)
        el = t->total_ms;
    t->elapsed_ms = el;
    t->left_ms = t->total_ms - el;
    t->step = count ? el / t->dwell_ms + 1u : 0u;
    t->step_left_ms = t->dwell_ms - el % t->dwell_ms;
    if (t->step > count) {   /* at the very end */
        t->step = count;
        t->step_left_ms = 0;
    }
}

int nx_runner_quiet(uint32_t need_ms)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct nx_run_times t;
        if (!nx_txq_idle(ch))
            return 0;
        nx_runner_times(ch, &t);
        if ((g_ui_flags & UIF_RUN(ch)) && t.step_left_ms < need_ms)
            return 0;
    }
    return 1;
}
