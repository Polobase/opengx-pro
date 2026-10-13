/* The manual generator (nx_manual.h). */
#include "nx_manual.h"

#include <string.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_kv.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_txq.h"

#if NX_FEAT_MANUAL
struct nx_manual g_nx_manual;

static const struct nx_manual k_defaults = {
    NX_MANUAL_VER, NX_MAN_FIXED, 1, 0, NX_SW_LOOP, 0, 50, 100000u, 10000u, 100000u, 60u,
};   /* sine, 5 V, 1000 Hz; the sweep 100 -> 1000 Hz in 60 s */

static uint8_t s_save_due;
static uint8_t s_reapply[2];   /* a waveform or amplitude change: outputs off, then on again */
static uint32_t s_tick[2];
static uint64_t s_sent[2];     /* the frequency code the generator has */

static uint32_t clamp_chz(uint32_t v)
{
    return v < 1u ? 1u : v > NX_MANUAL_MAX_CHZ ? NX_MANUAL_MAX_CHZ : v;
}

void nx_manual_boot(void)
{
    struct nx_manual *m = &g_nx_manual;
    s_save_due = s_reapply[0] = s_reapply[1] = 0;
    if (nx_kv_get(NX_KEY_MANUAL, m, sizeof *m) != (int)sizeof *m || m->ver != NX_MANUAL_VER)
        *m = k_defaults;
    if (m->mode > NX_MAN_SWEEP || m->wave < 1u || m->wave > 9u || m->shape > 1u || m->repeat > NX_SW_PINGPONG
        || m->ampl_dv > 200u || m->time_s < 1u || m->time_s > 86400u)
        *m = k_defaults;
    m->f_chz = clamp_chz(m->f_chz);
    m->start_chz = clamp_chz(m->start_chz);
    m->end_chz = clamp_chz(m->end_chz);
}

static uint64_t code_of(uint32_t chz)   /* Hz = chz * 10^-2 */
{
    return (uint64_t)chz * 10u + 6u;
}

int nx_manual_fill(struct program *p)
{
    const struct nx_manual *m = &g_nx_manual;
    memset(p, 0, sizeof *p);
    p->hdr[HDR_WAVE] = 10u + m->wave;
    p->hdr[HDR_AMPL] = m->ampl_dv * 10u;
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = 1;
    p->hdr[HDR_COUNT] = 1;
    p->freq[0] = code_of(m->mode == NX_MAN_SWEEP ? m->start_chz : m->f_chz);
    return NX_OK;
}

int nx_manual_play(unsigned ch)
{
    struct nx_action a = { NXA_LOAD, (uint8_t)(ch & 1u), NX_SRC_MANUAL, 0, 0 };
    int rc = nx_core_act(&a);
    if (rc == NX_OK) {
        a.type = NXA_PLAY;
        rc = nx_core_act(&a);
    }
    if (rc == NX_OK) {
        s_sent[ch & 1u] = g_ch[ch & 1u].prog.freq[0];
        s_reapply[ch & 1u] = 0;
    }
    return rc;
}

static void send_freq(unsigned ch, uint64_t code)
{
    if (nx_txq_submit_freq(ch, code) == NX_OK)
        s_sent[ch] = code;
}

void nx_manual_changed(void)
{
    s_save_due = 1;
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct channel *c = &g_ch[ch];
        if (c->prog_slot != NX_SRC_MANUAL)
            continue;
        uint64_t wave = c->prog.hdr[HDR_WAVE], ampl = c->prog.hdr[HDR_AMPL];
        nx_manual_fill(&c->prog);
        if (!(g_ui_flags & UIF_RUN(ch)) || s_reapply[ch])
            continue;   /* the next play sends it all */
        if (wave != c->prog.hdr[HDR_WAVE] || ampl != c->prog.hdr[HDR_AMPL]) {
            struct nx_action a = { NXA_PAUSE, (uint8_t)ch, 0, 0, 0 };   /* the idle configuration */
            if (nx_core_act(&a) == NX_OK)
                s_reapply[ch] = 1;
        } else if (g_nx_manual.mode == NX_MAN_FIXED && c->prog.freq[0] != s_sent[ch]) {
            send_freq(ch, c->prog.freq[0]);
        }
    }
}

uint32_t nx_manual_period_ms(void)
{
    return g_nx_manual.mode == NX_MAN_SWEEP ? g_nx_manual.time_s * 1000u : 0u;
}

uint32_t nx_manual_step(const struct channel *c)
{
    uint32_t t = nx_manual_period_ms();
    return t && g_nx_manual.repeat == NX_SW_ONCE && c->elapsed >= t ? 2u : 1u;
}

/* log2(x) in 1/65536 (x > 0) */
static int32_t log2_q16(uint32_t x)
{
    int ip = 31 - __builtin_clz(x);
    uint32_t y = (uint32_t)(((uint64_t)x << 30) >> ip);   /* x / 2^ip in [1, 2), Q30 */
    int32_t r = ip << 16;
    for (int32_t b = 1 << 15; b; b >>= 1) {
        y = (uint32_t)(((uint64_t)y * y) >> 30);
        if (y >= 2u << 30) {
            y >>= 1;
            r |= b;
        }
    }
    return r;
}

/* a * 2^(y / 65536) */
static uint32_t mul_exp2(uint32_t a, int32_t y)
{
    static const uint32_t k_root[16] = {   /* 2^(2^-i), i = 1..16, Q30 */
        1518500250u, 1276901417u, 1170923762u, 1121280436u, 1097253708u, 1085434106u, 1079572136u, 1076653033u,
        1075196443u, 1074468888u, 1074105294u, 1073923544u, 1073832680u, 1073787251u, 1073764537u, 1073753181u,
    };
    int32_t k = y >> 16;
    uint64_t m = 1u << 30;
    for (unsigned i = 0; i < 16u; i++)
        if (y & (0x8000 >> i))
            m = (m * k_root[i]) >> 30;
    uint64_t v = (uint64_t)a * m;
    v = k >= 0 ? v << k : v >> -k;
    v >>= 30;
    return v > NX_MANUAL_MAX_CHZ ? NX_MANUAL_MAX_CHZ : (uint32_t)v;
}

uint32_t nx_manual_chz(uint32_t el_ms)
{
    const struct nx_manual *m = &g_nx_manual;
    uint32_t T = nx_manual_period_ms();
    if (!T)
        return m->f_chz;
    uint32_t pass = el_ms / T, t = el_ms % T, a = m->start_chz, b = m->end_chz;
    if (pass && m->repeat == NX_SW_ONCE)
        return b;
    if ((pass & 1u) && m->repeat == NX_SW_PINGPONG)
        t = T - t;   /* on the way back */
    if (!m->shape)
        return (uint32_t)((int64_t)a + ((int64_t)b - a) * t / T);
    return mul_exp2(a, (int32_t)((int64_t)(log2_q16(b) - log2_q16(a)) * t / T));
}

void nx_manual_task(uint32_t now)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct channel *c = &g_ch[ch];
        if (c->prog_slot != NX_SRC_MANUAL || g_ui_state == ST_PC) {
            s_reapply[ch] = 0;
            continue;
        }
        if (s_reapply[ch]) {   /* the idle configuration has gone out: outputs on again */
            if (c->step_sent == 1u && nx_txq_idle(ch)) {
                struct nx_action a = { NXA_PLAY, (uint8_t)ch, 0, 0, 0 };
                nx_core_act(&a);
                s_sent[ch] = c->prog.freq[0];
                s_reapply[ch] = 0;
            }
            continue;
        }
        if (!(g_ui_flags & UIF_RUN(ch)) || g_nx_manual.mode != NX_MAN_SWEEP || now - s_tick[ch] < NX_SWEEP_TICK_MS
            || !nx_txq_idle(ch) || nx_manual_step(c) > 1u)
            continue;
        s_tick[ch] = now;
        c->prog.freq[0] = code_of(nx_manual_chz(c->elapsed));
        if (c->prog.freq[0] != s_sent[ch])
            send_freq(ch, c->prog.freq[0]);
    }
    if (s_save_due && g_ui_state != ST_PC && nx_runner_quiet(nx_kv_free() < 256u ? 1000u : 100u)) {
        nx_kv_put(NX_KEY_MANUAL, &g_nx_manual, sizeof g_nx_manual);
        s_save_due = 0;
    }
}
#else
void nx_manual_boot(void)
{
}

void nx_manual_task(uint32_t now)
{
    (void)now;
}
#endif
