/* Biofeedback scan (nx_bfb.h). */
#include "nx_bfb.h"

#include <string.h>

#include "link.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_txq.h"
#include "state.h"
#include "storage.h"

uint64_t nx_bfb_code(uint64_t f8)
{
    unsigned e = 0;
    while (e < 8u && f8 && f8 % 10u == 0u) {
        f8 /= 10u;
        e++;
    }
    return f8 * 10u + e;
}

/* The vendor's device authentication (Spooky.exe, decoded in the Hunt & Kill port): per token
 * digit i, (R[b] * R[c] + R[a] * R[challenge digit i]) % 9 + 1 with 1-based positions. */
void nx_bfb_auth_token(const char *chal, const char *resp, char *tok)
{
    static const uint8_t k[9][3] = {
        { 4, 6, 8 }, { 6, 4, 1 }, { 8, 6, 5 }, { 3, 2, 9 }, { 7, 8, 4 }, { 3, 1, 7 }, { 9, 4, 3 }, { 1, 6, 2 }, { 3, 2, 8 },
    };
    unsigned r[10] = { 0 };
    for (unsigned i = 0; i < 9u; i++)
        r[i + 1] = (unsigned)(resp[i] - '0') % 10u;
    for (unsigned i = 0; i < 9u; i++) {
        unsigned d = (unsigned)(chal[i] - '0') % 10u;
        tok[i] = (char)('0' + (r[k[i][1]] * r[k[i][2]] + r[k[i][0]] * r[d]) % 9u + 1u);
    }
    tok[9] = 0;
}

#if NX_FEAT_BFB
#define CHALLENGE "271543986"   /* any permutation of 1..9; this one is from a Spooky2 capture */
#define BASE_FAILS 5u          /* baseline reads without a reply in a row: the generator does not answer */

struct nx_bfb_cfg g_nx_bfb_cfg;

/* A reply ":rNN=<v0>[,<v1>]." of the generator, parsed in the receive interrupt. */
struct rx {
    uint8_t st, reg, n;
    uint32_t v[2];
    volatile uint8_t done;
};
static struct rx s_rx[2];

/* Hit detection as the readings come (nx_bfb.h): the window of the readings before the current
 * one, and the run of equal readings ("plateau") whose end decides whether it was a maximum. */
struct det {
    uint32_t win[NX_BFB_WINDOW];
    uint32_t sum;
    uint8_t wn, wi;         /* readings in the window, the oldest one's slot */
    uint8_t warm;           /* 1: scoring starts at index warm_at */
    uint8_t has_before, next_set;
    uint32_t n;             /* readings pushed */
    uint32_t warm_at;
    uint32_t before, plat;  /* the reading before the plateau; the plateau's reading */
    uint32_t plat_at;       /* index of the plateau's first reading */
    int32_t plat_dev20;     /* its deviation, in 1/20 of a reading unit (exact: reading * 20 - sum) */
    uint64_t next_code;     /* the frequency (:w24 code) of the step after the plateau's first reading */
    uint16_t next_step;     /* ... and that step (1-based) */
};

static struct {
    uint8_t sub;            /* the step within the state */
    uint8_t valid;          /* bit 0: r11 of the pair answered, bit 1: r12 */
    uint8_t miss_run;       /* baseline pairs without a reply in a row */
    uint16_t i;             /* ramp step, baseline pair */
    uint32_t t0, t1;        /* the step's :w24 submit time; its :ok time (the settle wait) */
    uint32_t r11, r12;
    uint64_t f8;
    struct det d;
    struct nx_bfb_view v;
} s;

void nx_bfb_boot(void)
{
    memset(&s, 0, sizeof s);
    memset(s_rx, 0, sizeof s_rx);
    s.v.steps = NX_BFB_STEPS;
    g_nx_bfb_cfg = (struct nx_bfb_cfg){ 0, PROGRAM_SLOTS, 2000u, 180u, 0 };   /* G1, slot 30, 20 V, 3 min per hit, standard */
}

const struct nx_bfb_view *nx_bfb_view(void)
{
    return &s.v;
}

void nx_bfb_rx(unsigned ch, uint8_t c)
{
    struct rx *r = &s_rx[ch & 1u];
    unsigned d = (unsigned)c - '0', st = r->st;
    r->st = 0;
    if (c == ':') {
        r->st = 1;
    } else if (st == 1u) {
        if (c == 'r')
            r->st = 2;
    } else if (st == 2u || st == 3u) {
        if (d <= 9u) {
            r->reg = (uint8_t)(st == 2u ? d * 10u : r->reg + d);
            r->st = (uint8_t)(st + 1u);
        }
    } else if (st == 4u) {
        if (c == '=') {
            r->st = 5;
            r->n = 0;
            r->v[0] = r->v[1] = 0;
        }
    } else if (st == 5u) {
        if (d <= 9u) {
            if (r->v[r->n] < 100000000u)   /* nine digits at most: the handshake's fields */
                r->v[r->n] = r->v[r->n] * 10u + d;
            r->st = 5;
        } else if (c == ',' && r->n == 0u) {
            r->n = 1;
            r->st = 5;
        } else if (c == '.') {
            r->done = 1;
            nx_txq_on_ok(ch & 1u);   /* the reply answers the queued read line */
        }
    }
}

static uint32_t ramp_cv(unsigned i)   /* amplitude step i of NX_BFB_RAMP_STEPS, 0 = the first */
{
    uint32_t t = g_nx_bfb_cfg.ampl_cv, cv = (2u * (i + 1u) * t + NX_BFB_RAMP_STEPS) / (2u * NX_BFB_RAMP_STEPS);
    return cv > t ? t : cv;
}

int nx_bfb_fill(struct program *p)
{
    memset(p, 0, sizeof *p);
    p->hdr[HDR_WAVE] = 11;   /* Spooky2's sine */
    p->hdr[HDR_AMPL] = ramp_cv(0);
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = 1;
    p->hdr[HDR_COUNT] = 1;
    p->freq[0] = nx_bfb_code(NX_BFB_START_F8);
    return NX_OK;
}

static void act(unsigned ch, uint8_t type, uint16_t id)
{
    struct nx_action a = { type, (uint8_t)ch, id, 0, 0 };
    nx_core_act(&a);
}

int nx_bfb_start(void)
{
    const struct nx_bfb_cfg *c = &g_nx_bfb_cfg;
    if (s.v.state != NX_BFB_IDLE && s.v.state < NX_BFB_DONE)
        return NX_BUSY;
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    if (!nx_core_ready() || g_ui_state == ST_CAL)
        return NX_BUSY;
    if (c->ch > 1u || c->slot < 1u || c->slot > PROGRAM_SLOTS)
        return NX_EINVAL;
    if (g_ch[c->ch ^ 1u].prog_slot == c->slot)   /* the other generator plays that slot */
        return NX_BUSY;
    if (g_ch[c->ch].prog_slot)   /* whatever it plays stops first: its idle configuration */
        act(c->ch, NXA_STOP, 0);
    memset(&s, 0, sizeof s);
    s.v.ch = c->ch;
    s.v.slot = c->slot;
    s.v.steps = NX_BFB_SPEED_STEPS(c->speed);
    s.v.state = NX_BFB_AUTH;
    return NX_OK;
}

void nx_bfb_stop(void)
{
    if (s.v.state == NX_BFB_IDLE || s.v.state >= NX_BFB_DONE)
        return;
    if (g_ch[s.v.ch].prog_slot == NX_SRC_BFB)
        act(s.v.ch, NXA_STOP, 0);
    s.v.state = NX_BFB_IDLE;
}

static void w_line(unsigned ch, unsigned id, uint32_t v)   /* ":wNN=v," */
{
    char buf[NX_TXQ_LINE_MAX];
    nx_txq_submit(ch, buf, link_fmt_w_u64(buf, id, v), 0);
}

static void line(unsigned ch, const char *s0)   /* s0 + "\r\n" */
{
    char buf[NX_TXQ_LINE_MAX];
    unsigned n = (unsigned)strlen(s0);
    memcpy(buf, s0, n);
    buf[n] = '\r';
    buf[n + 1u] = '\n';
    nx_txq_submit(ch, buf, n + 2u, 0);
}

static void read_line(unsigned ch, const char *s0)   /* a read: its reply is awaited */
{
    s_rx[ch].done = 0;
    line(ch, s0);
}

static int reply(unsigned ch, unsigned reg, uint32_t *v0, uint32_t *v1)   /* 1: the read was answered */
{
    struct rx *r = &s_rx[ch];
    if (!r->done || r->reg != reg)
        return 0;
    *v0 = r->v[0];
    if (v1)
        *v1 = r->v[1];
    return 1;
}

/* The :r11 / :r12 pair, one line per idle moment: 1 once both have been answered or not. */
static int pair_step(unsigned ch)
{
    uint32_t v;
    switch (s.sub) {
    case 0:
        read_line(ch, ":r11=,");
        s.valid = 0;
        s.sub = 1;
        return 0;
    case 1:
        if (reply(ch, 11, &v, 0)) {
            s.r11 = v;
            s.valid |= 1u;
        }
        read_line(ch, ":r12=,");
        s.sub = 2;
        return 0;
    default:
        if (reply(ch, 12, &v, 0)) {
            s.r12 = v;
            s.valid |= 2u;
        }
        s.sub = 0;
        return 1;
    }
}

static void trace_push(int32_t dev20)   /* a sweep reading into its column of the graph */
{
    struct nx_bfb_view *v = &s.v;
    int32_t u = dev20 / 20;
    unsigned col = (uint32_t)v->step * NX_BFB_TRACE / v->steps;
    if (col >= NX_BFB_TRACE)
        col = NX_BFB_TRACE - 1u;
    if (u > (int32_t)v->trace[col])
        v->trace[col] = (uint8_t)(u > 255 ? 255 : u);
    v->live[v->live_n % NX_BFB_TRACE] = (int8_t)(u > 127 ? 127 : u < -127 ? -127 : u);
    v->live_n++;
}

static void add_hit(uint64_t code, uint32_t dev20, uint16_t step)
{
    struct nx_bfb_view *v = &s.v;
    unsigned n = v->nhits, k = n;
    while (k > 0u && v->hits[k - 1u].dev20 < dev20)   /* the earlier hit stays first on a tie */
        k--;
    if (k >= NX_BFB_HITS)
        return;
    for (unsigned j = n < NX_BFB_HITS ? n : NX_BFB_HITS - 1u; j > k; j--)
        v->hits[j] = v->hits[j - 1u];
    v->hits[k] = (struct nx_bfb_hit){ code, dev20, step };
    if (n < NX_BFB_HITS)
        v->nhits++;
}

static void det_push(uint64_t code, uint32_t r)
{
    struct det *d = &s.d;
    uint32_t i = d->n;
    int32_t dev20 = 0;
    if (r > 10000000u)   /* readings are five digits; keeps the arithmetic below in 32 bits */
        r = 10000000u;
    if (d->wn == NX_BFB_WINDOW) {
        dev20 = (int32_t)(r * 20u) - (int32_t)d->sum;
        if (s.v.state == NX_BFB_SWEEP)
            trace_push(dev20);
        if (!d->warm && i <= 5u * NX_BFB_WINDOW && d->sum) {
            uint32_t lo = 0xFFFFFFFFu, hi = 0, a = (uint32_t)(dev20 < 0 ? -dev20 : dev20);
            for (unsigned k = 0; k < NX_BFB_WINDOW; k++) {
                lo = d->win[k] < lo ? d->win[k] : lo;
                hi = d->win[k] > hi ? d->win[k] : hi;
            }
            /* (hi - lo) <= mean / 100 and |reading - mean| <= mean / 100, with mean = sum / 20 */
            if ((hi - lo) * 2000u <= d->sum && a * 100u <= d->sum) {
                d->warm = 1;
                d->warm_at = i;
            }
        }
    }
    if (!d->warm && i > 5u * NX_BFB_WINDOW) {   /* never settled: from the first full window */
        d->warm = 1;
        d->warm_at = NX_BFB_WINDOW;
    }
    if (i == 0u) {
        d->plat = r;
    } else {
        if (!d->next_set) {
            d->next_code = code;
            d->next_step = (uint16_t)(s.v.step + 1u);
            d->next_set = 1;
        }
        if (r != d->plat) {
            /* a fall after a rise: a maximum, scored at the plateau's first reading if any of its
             * readings is past the settle */
            if (r < d->plat && d->has_before && d->before < d->plat && d->warm && i - 1u >= d->warm_at
                && d->plat_dev20 > 0)
                add_hit(d->next_code, (uint32_t)d->plat_dev20, d->next_step);
            d->before = d->plat;
            d->has_before = 1;
            d->plat = r;
            d->plat_dev20 = dev20;
            d->plat_at = i;
            d->next_set = 0;
        }
    }
    if (d->wn == NX_BFB_WINDOW)
        d->sum -= d->win[d->wi];
    else
        d->wn++;
    d->win[d->wi] = r;
    d->sum += r;
    d->wi = (uint8_t)((d->wi + 1u) % NX_BFB_WINDOW);
    d->n++;
}

static void hit_program(struct channel *c)
{
    struct program *p = &c->prog;
    memset(p, 0, sizeof *p);
    memset(c->params, 0, sizeof c->params);
    p->hdr[HDR_WAVE] = 11;
    p->hdr[HDR_AMPL] = g_nx_bfb_cfg.ampl_cv;
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = g_nx_bfb_cfg.dwell_s;
    p->hdr[HDR_COUNT] = s.v.nhits;
    for (unsigned i = 0; i < s.v.nhits; i++)
        p->freq[i] = s.v.hits[i].code;
}

static void sweep(unsigned ch, struct channel *c, uint32_t now)
{
    struct nx_bfb_view *v = &s.v;
    switch (s.sub) {
    case 0:   /* the next frequency */
        if (s.f8 > NX_BFB_END_F8) {
            v->state = NX_BFB_SAVE;
            break;
        }
        s.f8 += s.f8 / NX_BFB_SPEED_GRID(g_nx_bfb_cfg.speed);   /* 0.025 %, or 0.05 % at the quicker speeds */
        v->code = c->prog.freq[0] = c->step_freq = nx_bfb_code(s.f8);   /* the dashboard follows */
        nx_txq_submit_freq(ch, c->step_freq);
        s.t0 = now;
        s.sub = 1;
        break;
    case 1:   /* its :ok: the settle time starts */
        s.t1 = now;
        s.sub = 2;
        break;
    case 2:
        if (now - s.t1 < NX_BFB_SETTLE_MS)
            break;
        s.sub = 3;
        /* fall through */
    case 3:
    case 4:
    case 5:
        s.sub -= 3u;
        if (!pair_step(ch)) {
            s.sub += 3u;
            break;
        }
        if (s.valid == 3u)
            det_push(v->code, s.r11);
        else
            v->missed++;
        v->reading = s.r11;
        v->step++;
        s.sub = 6;
        /* fall through */
    default:   /* the step's time */
        if (now - s.t0 >= NX_BFB_SPEED_MS(g_nx_bfb_cfg.speed))
            s.sub = 0;
        break;
    }
}

void nx_bfb_task(uint32_t now)
{
    struct nx_bfb_view *v = &s.v;
    unsigned ch = v->ch;
    struct channel *c = &g_ch[ch];
    if (v->state == NX_BFB_IDLE || v->state >= NX_BFB_DONE)
        return;
    int loaded = v->state > NX_BFB_LOAD || (v->state == NX_BFB_LOAD && s.sub);
    if (g_ui_state == ST_PC || (loaded && (c->prog_slot != NX_SRC_BFB || !(g_ui_flags & UIF_RUN(ch))))) {
        v->state = NX_BFB_IDLE;   /* PC mode, STOP or another program ended it */
        return;
    }
    if (!nx_txq_idle(ch))   /* every line waits for the one before it: its :ok or its reply */
        return;
    switch (v->state) {
    case NX_BFB_AUTH: {
        uint32_t echo, resp;
        if (s.sub == 0u) {
            read_line(ch, ":r90=" CHALLENGE ",");
            s.sub = 1;
        } else if (s.sub == 1u && reply(ch, 90, &echo, &resp) && resp >= 111111111u) {
            char r[10], buf[20] = ":w92=";
            for (unsigned i = 9; i-- > 0; resp /= 10u)
                r[i] = (char)('0' + resp % 10u);
            r[9] = 0;
            nx_bfb_auth_token(CHALLENGE, r, buf + 5);
            buf[14] = '.';
            buf[15] = 0;
            line(ch, buf);
            s.sub = 2;
        } else {   /* no answer to the challenge (none needed, then), or the token's :ok is in */
            v->state = NX_BFB_LOAD;
            s.sub = 0;
        }
        break;
    }
    case NX_BFB_LOAD:
        if (s.sub == 0u) {
            struct nx_action a = { NXA_LOAD, (uint8_t)ch, NX_SRC_BFB, 0, 0 };
            if (nx_core_act(&a) != NX_OK) {
                v->state = NX_BFB_IDLE;
                break;
            }
            act(ch, NXA_PLAY, 0);
            s.sub = 1;
        } else if (c->step_sent == 1u) {   /* the step is out and answered: outputs on at the first ramp value */
            v->state = NX_BFB_RAMP;
            s.i = 1;
            s.sub = 0;
        }
        break;
    case NX_BFB_RAMP:
        if (s.i < NX_BFB_RAMP_STEPS) {
            uint32_t cv = ramp_cv(s.i++);
            w_line(ch, 28, cv);
            w_line(ch, 29, cv);
            c->prog.hdr[HDR_AMPL] = cv;   /* the dashboard follows */
        } else if (s.sub == 0u) {
            s.t0 = now;
            s.sub = 1;
        } else if (now - s.t0 >= NX_BFB_START_DELAY_MS) {
            v->state = NX_BFB_BASE;
            s.i = 0;
            s.sub = 0;
        }
        break;
    case NX_BFB_BASE:
        if (!pair_step(ch))
            break;
        if (s.valid == 3u) {
            s.miss_run = 0;
            v->reading = s.r11;
            if (s.i + NX_BFB_WINDOW >= NX_BFB_BASELINE)   /* the last window of the baseline seeds the detection */
                det_push(0, s.r11);
        } else {
            v->missed++;
            if (++s.miss_run >= BASE_FAILS) {
                act(ch, NXA_STOP, 0);
                v->state = NX_BFB_FAILED;
                break;
            }
        }
        if (++s.i >= NX_BFB_BASELINE) {
            v->state = NX_BFB_SWEEP;
            v->sweep_ms = now;
            memset(v->trace, 0, sizeof v->trace);
            v->live_n = 0;
            s.f8 = NX_BFB_START_F8;
            s.sub = 0;
        }
        break;
    case NX_BFB_SWEEP:
        sweep(ch, c, now);
        break;
    default:   /* NX_BFB_SAVE: the slot write blocks up to about 400 ms; the other generator's step must not be due */
        if (!nx_runner_quiet(1000u))
            break;
        if (v->nhits) {
            static const char k_name[] = NX_BFB_NAME " G1";
            char name[sizeof k_name];
            memcpy(name, k_name, sizeof name);
            name[sizeof name - 2u] = (char)('1' + ch);
            hit_program(c);
            program_save(v->slot, ch);
            name_set(v->slot, name, sizeof name - 1u);
            names_save();
            act(ch, NXA_LOAD, v->slot);
            act(ch, NXA_PLAY, 0);
        } else {
            act(ch, NXA_STOP, 0);
        }
        v->state = NX_BFB_DONE;
        break;
    }
}
#else
void nx_bfb_boot(void)
{
}

void nx_bfb_task(uint32_t now)
{
    (void)now;
}
#endif
