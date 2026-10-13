/* Built-in Terrain Protocol (nx_terrain.h). */
#include "nx_terrain.h"

#include <string.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_hist.h"
#include "nx_kv.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_timer.h"
#include "nx_ui.h"
#include "nx_vm.h"

#if !NX_TERRAIN_DATA
const struct nx_tdata k_nx_terrain;   /* a build without the presets: no sets, no protocols */
#endif

#define T k_nx_terrain

static const struct nx_tset *tset(unsigned set)
{
    return set < T.nset ? &T.set[set] : 0;
}

static unsigned body(const struct nx_tset *s)
{
    return (unsigned)s->n * s->k;
}

unsigned nx_tset_steps(unsigned set)
{
    const struct nx_tset *s = tset(set);
    return s ? body(s) + (s->end != NX_TSET_NO_END) : 0u;
}

uint32_t nx_tset_dwell_s(unsigned set, unsigned i)
{
    const struct nx_tset *s = tset(set);
    if (!s || !s->n)
        return 1;
    uint32_t d = T.dwell[i < body(s) ? T.dwi[s->dw + i % s->n] : s->end];
    return d ? d : 1u;
}

int nx_tset_fill(unsigned set, uint16_t ampl_cv, struct program *p)
{
    const struct nx_tset *s = tset(set);
    unsigned n = nx_tset_steps(set);
    if (!s || !s->n || n > PROGRAM_STEPS)
        return NX_EINVAL;
    memset(p, 0, sizeof *p);
    p->hdr[HDR_WAVE] = s->wave;
    p->hdr[HDR_AMPL] = ampl_cv ? ampl_cv : s->dv * 10u;
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = nx_tset_dwell_s(set, 0);
    p->hdr[HDR_COUNT] = n;
    for (unsigned i = 0; i < body(s); i++) {   /* the end step stays 0 Hz */
        const uint8_t *f = &T.freq[3u * (s->list + i % s->n)];
        uint32_t c = f[0] | (uint32_t)f[1] << 8 | (uint32_t)f[2] << 16;
        p->freq[i] = c ? (uint64_t)c * 10u + 6u : 0u;   /* Hz = c * 10^-2 */
    }
    return NX_OK;
}

const char *nx_tset_name(unsigned set)
{
    const struct nx_tset *s = tset(set);
    return s ? &T.str[s->name] : "";
}

/* ---- the protocols ---- */

#if NX_FEAT_TERRAIN
struct nx_terrain_rec g_nx_terrain;

static struct {
    struct nx_trun r;    /* what plays; r.id 0 = no run */
    uint8_t slot;        /* the source the run loaded */
    uint8_t end_seen;    /* Contact: the session has counted */
    uint8_t done;        /* the program ended by itself */
    uint32_t ms;         /* Remote: running time in the item, ms */
    uint32_t last;       /* nx_ms() of the last count, 0 while not running */
} s_run[2];
static uint8_t s_save_due;
static uint32_t s_saved_t;

static const uint16_t k_power_cv[NX_TERRAIN_POWERS] = { 0, 250, 100, 50, 25 };

unsigned nx_terrain_count(void)
{
    return T.nproto < NX_TERRAIN_MAX ? T.nproto : NX_TERRAIN_MAX;
}

const struct nx_tproto *nx_terrain_proto(unsigned id)
{
    return id >= 1u && id <= nx_terrain_count() ? &T.proto[id - 1u] : 0;
}

unsigned nx_terrain_items(unsigned id)
{
    const struct nx_tproto *p = nx_terrain_proto(id);
    return !p ? 0u : p->kind == NX_TP_REMOTE ? p->n : p->n * 2u;
}

unsigned nx_terrain_set_of(unsigned id, unsigned item)
{
    const struct nx_tproto *p = nx_terrain_proto(id);
    if (!p || item >= nx_terrain_items(id))
        return 0xFFu;
    return T.item[p->item + (p->kind == NX_TP_REMOTE ? 3u * item : item)];
}

uint16_t nx_terrain_ampl(unsigned power)
{
    return power < NX_TERRAIN_POWERS ? k_power_cv[power] : 0u;
}

const char *nx_terrain_name(unsigned id)
{
    const struct nx_tproto *p = nx_terrain_proto(id);
    return p ? &T.str[p->name] : "";
}

void nx_terrain_where(unsigned id, unsigned item, uint8_t *day, uint8_t *part, uint8_t *hours)
{
    const struct nx_tproto *p = nx_terrain_proto(id);
    *day = *part = *hours = 0;
    if (!p || item >= nx_terrain_items(id))
        return;
    if (p->kind == NX_TP_REMOTE) {
        const uint8_t *it = &T.item[p->item + 3u * item];
        *hours = it[1];
        *day = (uint8_t)(it[2] >> 2);
        *part = (uint8_t)(it[2] & 3u);
    } else {
        *day = (uint8_t)(item / 2u + 1u);
        *part = (uint8_t)(item % 2u + 1u);
    }
}

void nx_terrain_boot(void)
{
    memset(s_run, 0, sizeof s_run);
    s_save_due = 0;
    s_saved_t = 0;
    if (nx_kv_get(NX_KEY_TERRAIN, &g_nx_terrain, sizeof g_nx_terrain) != (int)sizeof g_nx_terrain
        || g_nx_terrain.ver != NX_TERRAIN_VER)
        memset(&g_nx_terrain, 0, sizeof g_nx_terrain);
    g_nx_terrain.ver = NX_TERRAIN_VER;
    for (unsigned i = 0; i < NX_TERRAIN_MAX; i++) {   /* other presets since: start again */
        struct nx_tpos *p = &g_nx_terrain.p[i];
        if (p->item > nx_terrain_items(i + 1u) || p->power >= NX_TERRAIN_POWERS)
            memset(p, 0, sizeof *p);
    }
    for (unsigned ch = 0; ch < 2u; ch++)
        if (g_nx_terrain.active[ch] > nx_terrain_count())
            g_nx_terrain.active[ch] = 0;
}

static int remote(unsigned id)
{
    return nx_terrain_proto(id)->kind == NX_TP_REMOTE;
}

/* A Remote run's time and item into its stored position. */
static void sync(unsigned ch)
{
    if (!s_run[ch].r.id || !remote(s_run[ch].r.id))
        return;
    struct nx_tpos *p = &g_nx_terrain.p[s_run[ch].r.id - 1u];
    p->item = s_run[ch].r.item;
    p->run_s = s_run[ch].r.run_s = s_run[ch].ms / NX_DWELL_UNITS;
    s_save_due = 1;
}

/* Plays the stored position of id on ch (its run time goes on from the stored one). */
static int load_play(unsigned ch, unsigned id)
{
    struct nx_tpos *p = &g_nx_terrain.p[id - 1u];
    int rem = remote(id);
    struct nx_action a = { NXA_LOAD, (uint8_t)ch, (uint16_t)(NX_SRC_SET + nx_terrain_set_of(id, p->item)),
                           rem ? k_power_cv[p->power] : 0u, 0 };
    s_run[ch].r.id = 0;   /* the load below must not end a run that is not started yet */
    int rc = nx_core_act(&a);
    if (rc == NX_OK) {
        a.type = NXA_PLAY;
        a.v = (uint32_t)rem;   /* a Remote set loops until its hours are done */
        rc = nx_core_act(&a);
    }
    if (rc != NX_OK)
        return rc;
    memset(&s_run[ch], 0, sizeof s_run[ch]);
    s_run[ch].r = (struct nx_trun){ (uint8_t)id, p->item, p->power, p->run_s };
    s_run[ch].slot = (uint8_t)a.id;
    s_run[ch].ms = p->run_s * NX_DWELL_UNITS;
    g_nx_terrain.active[ch] = (uint8_t)id;
    s_save_due = 1;
    return NX_OK;
}

int nx_terrain_play(unsigned ch, unsigned id)
{
    if (ch > 1u || !nx_terrain_items(id))
        return NX_EINVAL;
    struct nx_tpos *p = &g_nx_terrain.p[id - 1u];
    if (p->item >= nx_terrain_items(id)) {   /* complete: day 1 again */
        p->item = 0;
        p->run_s = 0;
    }
    return load_play(ch, id);
}

int nx_terrain_continue(unsigned ch)
{
    return nx_terrain_play(ch & 1u, g_nx_terrain.active[ch & 1u]);
}

/* A generator that plays id now plays its (new) stored position. */
static void replay(unsigned id)
{
    for (unsigned ch = 0; ch < 2u; ch++)
        if (s_run[ch].r.id == id && load_play(ch, id) != NX_OK)
            s_run[ch].r.id = 0;
}

void nx_terrain_move(unsigned id, int delta)
{
    int n = (int)nx_terrain_items(id);
    if (!n)
        return;
    uint8_t playing = 0;
    for (unsigned ch = 0; ch < 2u; ch++)
        if (s_run[ch].r.id == id) {
            playing |= (uint8_t)(1u << ch);
            s_run[ch].r.id = 0;   /* a jump: the item's time starts again */
        }
    struct nx_tpos *p = &g_nx_terrain.p[id - 1u];
    int i = p->item >= n ? (delta > 0 ? -1 : n) : p->item;
    i += delta > 0 ? 1 : -1;
    p->item = (uint8_t)(i < 0 ? n - 1 : i >= n ? 0 : i);
    p->run_s = 0;
    s_save_due = 1;
    for (unsigned ch = 0; ch < 2u; ch++)
        if (playing & (1u << ch))
            load_play(ch, id);
}

void nx_terrain_power(unsigned id)
{
    if (!nx_terrain_items(id) || !remote(id))
        return;
    for (unsigned ch = 0; ch < 2u; ch++)
        if (s_run[ch].r.id == id)
            sync(ch);
    struct nx_tpos *p = &g_nx_terrain.p[id - 1u];
    p->power = (uint8_t)((p->power + 1u) % NX_TERRAIN_POWERS);
    s_save_due = 1;
    replay(id);
}

void nx_terrain_finished(unsigned ch)
{
    if (s_run[ch & 1u].r.id)
        s_run[ch & 1u].done = 1;
}

/* Contact: the session has counted; the stored position moves on (once, also after a resume). */
static void session_done(unsigned ch)
{
    struct nx_trun *r = &s_run[ch].r;
    struct nx_tpos *p = &g_nx_terrain.p[r->id - 1u];
    s_run[ch].end_seen = 1;
    if (p->item == r->item) {
        p->item = (uint8_t)(r->item + 1u);   /* the number of sessions: complete */
        s_save_due = 1;
    }
    nx_ui_post(NXE_ALERT, (uint8_t)ch, p->item);
}

void nx_terrain_task(uint32_t now)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct nx_trun *r = &s_run[ch].r;
        if (!r->id)
            continue;
        const struct channel *c = &g_ch[ch];
        int run = (g_ui_flags & UIF_RUN(ch)) != 0;
        if (g_ui_state == ST_PC || c->prog_slot != s_run[ch].slot) {
            sync(ch);
            r->id = 0;   /* PC mode or another program */
            continue;
        }
        if (remote(r->id)) {
            if (s_run[ch].last && run)
                s_run[ch].ms += now - s_run[ch].last;
            s_run[ch].last = run ? (now ? now : 1u) : 0u;
            const struct nx_tproto *p = nx_terrain_proto(r->id);
            if (s_run[ch].ms / NX_DWELL_UNITS >= T.item[p->item + 3u * r->item + 1u] * 3600u) {
                nx_hist_end(ch, NX_HEND_FINISHED);   /* the item has run its hours */
                r->item = (uint8_t)((r->item + 1u) % p->n);
                s_run[ch].ms = 0;
                sync(ch);
                unsigned id = r->id;
                if (load_play(ch, id) == NX_OK)
                    nx_timer_goes_on();   /* the protocol goes on: nothing has "all finished" */
                else
                    s_run[ch].r.id = 0;
                continue;
            }
            if (run && now - s_saved_t >= NX_TERRAIN_SAVE_MS)
                sync(ch);
        } else if (!s_run[ch].end_seen && run) {
            unsigned set = c->prog_slot - NX_SRC_SET, n = nx_tset_steps(set);
            if (T.set[set].end != NX_TSET_NO_END ? c->step >= n : s_run[ch].done)
                session_done(ch);
        }
        s_run[ch].done = 0;
        if (!run && c->elapsed <= 1u) {
            sync(ch);
            r->id = 0;   /* stopped, or the Contact set has ended */
        }
    }
    if (s_save_due && g_ui_state != ST_PC && nx_runner_quiet(nx_kv_free() < 256u ? 1000u : 100u)) {
        nx_kv_put(NX_KEY_TERRAIN, &g_nx_terrain, sizeof g_nx_terrain);
        s_save_due = 0;
        s_saved_t = now;
    }
}

void nx_terrain_run(unsigned ch, struct nx_trun *r)
{
    ch &= 1u;
    *r = s_run[ch].r;
    if (r->id && remote(r->id))
        r->run_s = s_run[ch].ms / NX_DWELL_UNITS;
}

void nx_terrain_resume(unsigned ch, const struct nx_trun *r)
{
    ch &= 1u;
    if (!r->id || !nx_terrain_items(r->id) || NX_SRC_SET + nx_terrain_set_of(r->id, r->item) != g_ch[ch].prog_slot)
        return;   /* other presets since: the set plays on its own */
    memset(&s_run[ch], 0, sizeof s_run[ch]);
    s_run[ch].r = *r;
    s_run[ch].slot = g_ch[ch].prog_slot;
    s_run[ch].ms = r->run_s * NX_DWELL_UNITS;
    g_nx_terrain.active[ch] = r->id;
}
#else
uint16_t nx_terrain_ampl(unsigned power)
{
    (void)power;
    return 0;
}

int nx_terrain_continue(unsigned ch)
{
    (void)ch;
    return NX_EINVAL;
}

void nx_terrain_boot(void)
{
}

void nx_terrain_finished(unsigned ch)
{
    (void)ch;
}

void nx_terrain_task(uint32_t now)
{
    (void)now;
}

void nx_terrain_run(unsigned ch, struct nx_trun *r)
{
    (void)ch;
    memset(r, 0, sizeof *r);
}

void nx_terrain_resume(unsigned ch, const struct nx_trun *r)
{
    (void)ch;
    (void)r;
}
#endif
