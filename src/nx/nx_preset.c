/* Built-in presets with sweeps (nx_preset.h). */
#include "nx_preset.h"

#include <string.h>

#include "link.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_gfx.h"
#include "nx_manual.h"
#include "nx_src.h"
#include "nx_txq.h"
#include "nx_ui.h"
#include "nx_uifmt.h"
#include "nx_vm.h"

#if !NX_PRESET_DATA
const struct nx_pdata k_nx_presets;   /* a build with NX_PRESETS=0: none */
#endif

#define P k_nx_presets

unsigned nx_preset_count(void)   /* at most NX_SRC_PRESETS: the generator tool and the fixture see to it */
{
    return P.n;
}

static const struct nx_preset *preset(unsigned n)
{
    return n < nx_preset_count() ? &P.preset[n] : 0;
}

static uint32_t u24(const uint8_t *b)
{
    return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
}

const char *nx_preset_name(unsigned n)
{
    const struct nx_preset *p = preset(n);
    return p ? &P.str[p->name] : "";
}

unsigned nx_preset_info(unsigned n)
{
    const struct nx_preset *p = preset(n);
    return p ? p->n | (unsigned)p->loop << 8 : 0u;
}

uint32_t nx_preset_dwell_s(unsigned n, unsigned i)
{
    const struct nx_preset *p = preset(n);
    return p && i < p->n ? P.step[p->step + i].dwell : 1u;   /* the table has no step under a second */
}

/* The wire code of chz (0.01 Hz) on a table of p's cycles. */
static uint64_t wire(const struct nx_preset *p, uint32_t chz)
{
    uint64_t m = ((uint64_t)chz * 1000000u + p->cycles / 2u) / p->cycles;   /* 1e-8 Hz */
    unsigned e = 0;
    while (m && e < 9u && m % 10u == 0) {   /* as Spooky2 writes it: 36 Hz is 368, not 36000000000 */
        m /= 10u;
        e++;
    }
    return m * 10u + e;
}

/* The frequency (0.01 Hz) of p's step i (wrapping), t of its T ms into it. */
static uint32_t chz_at(const struct nx_preset *p, unsigned i, uint32_t t, uint32_t T)
{
    const struct nx_pstep *s = &P.step[p->step + i % p->n];
    int32_t a = (int32_t)u24(s->a);
    return (uint32_t)(a + (int32_t)(((int64_t)u24(s->b) - a) * t / T));
}

int nx_preset_fill(unsigned n, struct program *prog)
{
    const struct nx_preset *p = preset(n);
    if (!p || !p->n || !p->cycles || p->n > PROGRAM_STEPS)
        return NX_EINVAL;
    memset(prog, 0, sizeof *prog);
    prog->hdr[HDR_WAVE] = p->wave1;
    prog->hdr[HDR_AMPL] = p->ampl_cv;
    prog->hdr[HDR_OFFS] = p->offs1;
    prog->hdr[HDR_DWELL] = nx_preset_dwell_s(n, 0);
    prog->hdr[HDR_COUNT] = p->n;
    for (unsigned i = 0; i < p->n; i++)   /* each step's start; a sweep's lines follow from the task */
        prog->freq[i] = wire(p, chz_at(p, i, 0, 1));
    return NX_OK;
}

/* floor(511.5 * sin(2 pi r / 1024)) for the quarter phase r = 0..256. nx_sin1024 is linear
 * between 65 points of the quarter wave: with the bias, four phases land on the wrong side of
 * an integer (the host test compares every sample of the tables with the formulas). */
static unsigned mag(unsigned r)
{
    unsigned m = (unsigned)(nx_sin1024(r) * 2046 + 1400) >> 16;
    if (r == 136u || r == 212u)
        m--;
    if (r == 230u || r == 249u)
        m++;
    return m;
}

unsigned nx_preset_sample(unsigned cycles, unsigned i, int inv)
{
    unsigned half = cycles != 0;   /* a preset's table: half scale */
    unsigned ph = ((half ? cycles : 1u) * i) & 1023u, r = ph & 255u;
    if (ph & 256u)
        r = 256u - r;
    unsigned m = mag(r) >> half;
    /* Spooky2's tables at the middle of the wave: 511 in its standard sine, 512 in the preset's */
    unsigned v = ph < 512u + half ? 512u + m : 511u - m;
    return inv ? 1023u - v : v;
}

#if NX_FEAT_PRESET
enum {
    PS_TABLES,   /* after a load: the wave tables are due */
    PS_UPLOAD,   /* a table is on its way, or about to start */
    PS_ACK,      /* it is out: its :ok, seen by nx_preset_setup once the link is idle */
    PS_LINES,    /* the tables are in the generator; the Out2 lines are due */
    PS_SENT,     /* they are on their way: once the link is idle again, the step */
    PS_READY,    /* its step is out or goes out now: its clock runs */
};

struct pst {
    uint8_t st;
    uint8_t tab;      /* the table of the upload: 0 Out1's, 1 Out2's (inverted) */
    uint8_t open;     /* its line has begun: it is finished whatever happens to the program */
    uint8_t nn, cyc;  /* the open line's table number and cycles */
    uint16_t i;       /* its next piece: 0 the head, 1..1024 the samples, then the end */
    uint32_t touts;   /* the link's timeouts before the tables */
    uint32_t tick;    /* the last sweep line */
};
static struct pst s_p[2];
volatile uint8_t g_nx_preset_hold;
/* The generator's table 11 holds Spooky2's standard sine, put there by nx. Not known after
 * power-on and after a PC session (Spooky2 uploads its tables with every preset, with several
 * cycles when the preset says so), and not so after a preset of nx. */
static uint8_t s_plain[2];

static const struct nx_preset *playing(unsigned ch)
{
    unsigned id = g_ch[ch].prog_slot;
    return nx_src_is_preset(id) ? preset(id - NX_SRC_PRESET) : 0;
}

static void hold(unsigned ch, int on)
{
    uint8_t bit = (uint8_t)(1u << ch);
    g_nx_preset_hold = (uint8_t)(on ? g_nx_preset_hold | bit : g_nx_preset_hold & ~bit);
}

void nx_preset_boot(void)
{
    memset(s_p, 0, sizeof s_p);
    g_nx_preset_hold = 0;
    s_plain[0] = s_plain[1] = 0;
}

/* The manual generator or a Terrain set that plays the sine (table 11): it gets the standard one. */
static int wants_sine(unsigned ch)
{
    return g_ch[ch].prog_slot > PROGRAM_SLOTS && g_ch[ch].prog.hdr[HDR_WAVE] == 11u;
}

void nx_preset_loaded(unsigned ch)
{
    s_p[ch].st = PS_TABLES;
    hold(ch, playing(ch) != 0);   /* a preset's clock waits from its load: its first step starts at 0 s */
}

static __attribute__((noinline)) void line(unsigned ch, unsigned id, unsigned v)   /* ":wNN=v," */
{
    char buf[NX_TXQ_LINE_MAX];
    nx_txq_submit(ch, buf, link_fmt_w_u64(buf, id, v), 0);
}

/* The runner asks before a step of a running program goes out (its link is idle then). */
int nx_preset_setup(unsigned ch)
{
    struct pst *s = &s_p[ch];
    const struct nx_preset *p = playing(ch);
    if (!p && (s_plain[ch] || !wants_sine(ch)))
        return 1;   /* a slot, another waveform, or the sine is there */
    if (p && s->st >= PS_SENT) {
        s->st = PS_READY;
        return 1;
    }
    if (s->st == PS_TABLES) {
        s->touts = nx_txq_stats(ch)->timeouts;
        s->tab = 0;
        s->st = PS_UPLOAD;
    } else if (s->st == PS_ACK) {   /* a table is out and answered, or not */
        if (!p) {   /* the standard sine: it plays in any case, and is not sent again */
            s_plain[ch] = 1;
            s->st = PS_READY;
            return 1;
        }
        if (nx_txq_stats(ch)->timeouts != s->touts) {   /* the generator did not take it */
            struct nx_action a = { NXA_STOP, (uint8_t)ch, 0, 0, 0 };
            nx_core_act(&a);
            s->st = PS_TABLES;
            nx_ui_post(NXE_NO_TABLE, (uint8_t)ch, 0);
        } else {
            s->st = s->tab ? PS_LINES : PS_UPLOAD;
            s->tab = 1;
        }
    } else if (s->st == PS_LINES) {   /* as Spooky2 has them: Out2 inverted by its table, */
        nx_txq_submit(ch, ":w17=0,0,\r\n", 11, 0);   /* not by the generator */
        line(ch, 21, p->wave2);
        line(ch, 33, p->offs2);
        s->st = PS_SENT;
    }
    return 0;
}

uint64_t nx_preset_code(unsigned ch)
{
    const struct nx_preset *p = playing(ch);
    struct nx_run_times t;
    if (!p)
        return 0;
    nx_runner_times(ch, &t);
    return wire(p, chz_at(p, t.step - 1u, t.dwell_ms - t.step_left_ms, t.dwell_ms));
}

void nx_preset_shown(unsigned ch, const struct nx_run_times *t, uint64_t *freq, uint64_t *next)
{
    const struct nx_preset *p = playing(ch);
    if (!p || !t->step)
        return;
    /* in steps of 128 ms, about the sweep's lines: the big digits change 8 times a second */
    *freq = chz_at(p, t->step - 1u, (t->dwell_ms - t->step_left_ms) & ~127u, t->dwell_ms) * 10u + 6u;
    if (*next)
        *next = chz_at(p, t->step, 0, 1) * 10u + 6u;
}

/* The open table line: as many pieces as the UART takes now. */
static void feed(unsigned ch, struct pst *s)
{
    while (s->i <= NX_PRESET_SAMPLES + 1u) {
        char buf[8], *e = buf;
        if (!s->i) {
            *e++ = ':';
            *e++ = 'a';
            e = nx_fmt_2d(e, s->nn);
            *e++ = '=';
        } else if (s->i <= NX_PRESET_SAMPLES) {
            e = nx_fmt_u(e, nx_preset_sample(s->cyc, s->i - 1u, s->tab));
            *e++ = ',';
        } else {
            *e++ = '\r';
            *e++ = '\n';
        }
        if (!nx_txq_stream_put(ch, buf, (unsigned)(e - buf)))
            return;
        s->i++;
    }
    nx_txq_stream_end(ch);
    s->open = 0;
    if (s->st == PS_UPLOAD)
        s->st = PS_ACK;
}

void nx_preset_task(uint32_t now)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct pst *s = &s_p[ch];
        struct channel *c = &g_ch[ch];
        const struct nx_preset *p = playing(ch);
        /* Not running, the generator has the runner's idle configuration: Out2 on Out1's table and
         * offset again, so the Out2 lines are due before the next step. */
        if (s->st >= PS_SENT && !(g_ui_flags & UIF_RUN(ch)))
            s->st = PS_LINES;
        /* the clock waits: a preset until its step goes out, anything else while its sine goes out */
        hold(ch, s->st != PS_READY && (p || (unsigned)(s->st - PS_UPLOAD) < 2u));
        if (s->open) {
            feed(ch, s);
        } else if (g_ui_state == ST_PC) {
            s_plain[ch] = 0;   /* the PC's tables now */
        } else if (s->st == PS_UPLOAD) {   /* not while a PC-mode entry waits for the link: no new line */
            if (nx_core_runner_held() || nx_txq_stream_begin(ch) != NX_OK)
                continue;
            s->open = 1;
            s->i = 0;
            s->nn = p ? (s->tab ? p->wave2 : p->wave1) : 11u;
            s->cyc = p ? p->cycles : 0u;   /* no preset: the standard sine into table 11 */
            s_plain[ch] = 0;
        } else if (p && s->st == PS_READY && c->step_sent == c->step && now - s->tick >= NX_SWEEP_TICK_MS
                   && nx_txq_idle(ch)) {
            /* a sweep step: the frequency of now, as nx_manual_task sends it */
            s->tick = now;
            uint64_t code = nx_preset_code(ch);
            if (code != c->step_freq && nx_txq_submit_freq(ch, code) == NX_OK)
                c->step_freq = code;
        }
    }
}
#else
void nx_preset_boot(void)
{
}

void nx_preset_task(uint32_t now)
{
    (void)now;
}
#endif
