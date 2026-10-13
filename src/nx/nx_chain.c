/* Chains (nx_chain.h). */
#include "nx_chain.h"

#include <string.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_kv.h"
#include "nx_runner.h"
#include "nx_timer.h"
#include "state.h"

#if NX_FEAT_CHAINS
struct nx_chains_rec g_nx_chains;
#if NX_FEAT_PLANS
struct nx_plans_rec g_nx_plans;
#endif

static struct {
    struct nx_chain_pos p;
    uint8_t slot;       /* the slot the chain loaded */
    uint8_t next_due;   /* its program ended: load the next one */
} s_run[2];
static uint8_t s_save_due;

void nx_chain_boot(void)
{
    memset(s_run, 0, sizeof s_run);
    s_save_due = 0;
    if (nx_kv_get(NX_KEY_CHAINS, &g_nx_chains, sizeof g_nx_chains) != (int)sizeof g_nx_chains
        || g_nx_chains.ver != NX_CHAIN_VER)
        memset(&g_nx_chains, 0, sizeof g_nx_chains);
    g_nx_chains.ver = NX_CHAIN_VER;
    for (unsigned i = 0; i < NX_CHAINS; i++)   /* a damaged record plays nothing wrong */
        if (g_nx_chains.c[i].n > NX_CHAIN_ITEMS)
            g_nx_chains.c[i].n = 0;
#if NX_FEAT_PLANS
    if (nx_kv_get(NX_KEY_PLANS, &g_nx_plans, sizeof g_nx_plans) != (int)sizeof g_nx_plans
        || g_nx_plans.ver != NX_CHAIN_VER)
        memset(&g_nx_plans, 0, sizeof g_nx_plans);
    g_nx_plans.ver = NX_CHAIN_VER;
    for (unsigned i = 0; i < NX_PLANS; i++) {
        struct nx_plan *p = &g_nx_plans.p[i];
        if (p->n > NX_PLAN_DAYS)
            p->n = 0;
        if (p->day > p->n)
            p->day = 0;
    }
#endif
}

static int load_play(unsigned ch, unsigned slot)
{
    struct nx_action a = { NXA_LOAD, (uint8_t)ch, (uint16_t)slot, 0, 0 };
    int rc = nx_core_act(&a);
    if (rc == NX_OK) {
        a.type = NXA_PLAY;
        rc = nx_core_act(&a);
    }
    if (rc == NX_OK)
        s_run[ch].slot = (uint8_t)slot;
    return rc;
}

int nx_chain_play(unsigned ch, unsigned id)
{
    if (ch > 1u || id < 1u || id > NX_CHAINS || !g_nx_chains.c[id - 1u].n)
        return NX_EINVAL;
    s_run[ch].p.id = 0;   /* the load below must not end a chain that is not started yet */
    int rc = load_play(ch, g_nx_chains.c[id - 1u].slot[0]);
    if (rc == NX_OK) {
        s_run[ch].p = (struct nx_chain_pos){ (uint8_t)id, 0, 0, 0 };
        s_run[ch].next_due = 0;
    }
    return rc;
}

#if NX_FEAT_PLANS
int nx_plan_play(unsigned ch, unsigned id)
{
    if (ch > 1u || id < 1u || id > NX_PLANS)
        return NX_EINVAL;
    const struct nx_plan *p = &g_nx_plans.p[id - 1u];
    if (p->day >= p->n)
        return NX_EINVAL;   /* empty or complete */
    int rc = nx_chain_play(ch, p->chain[p->day]);
    if (rc == NX_OK)
        s_run[ch].p.plan = (uint8_t)id;
    return rc;
}
#endif

void nx_chain_finished(unsigned ch)
{
    if (s_run[ch & 1u].p.id)
        s_run[ch & 1u].next_due = 1;
}

void nx_chain_save(void)
{
    s_save_due = 3u;   /* the chains and the plans */
}

void nx_chain_pos(unsigned ch, struct nx_chain_pos *p)
{
    *p = s_run[ch & 1u].p;
}

void nx_chain_resume(unsigned ch, const struct nx_chain_pos *p)
{
    ch &= 1u;
    if (!p->id || p->id > NX_CHAINS || p->item >= g_nx_chains.c[p->id - 1u].n
        || g_nx_chains.c[p->id - 1u].slot[p->item] != g_ch[ch].prog_slot)
        return;   /* the chain changed since: the program plays on its own */
    s_run[ch].p = *p;
    s_run[ch].slot = g_ch[ch].prog_slot;
    s_run[ch].next_due = 0;
}

static void advance(unsigned ch)
{
    struct nx_chain_pos *p = &s_run[ch].p;
    const struct nx_chain *c = &g_nx_chains.c[p->id - 1u];
    if (++p->item >= c->n) {
        p->item = 0;
        if (p->pass < 255u)
            p->pass++;
        if (c->repeat && p->pass >= c->repeat) {
            p->id = 0;   /* done */
#if NX_FEAT_PLANS
            if (p->plan && g_nx_plans.p[p->plan - 1u].day < g_nx_plans.p[p->plan - 1u].n) {
                g_nx_plans.p[p->plan - 1u].day++;   /* the day is done */
                s_save_due |= 2u;
            }
#endif
            return;
        }
    }
    if (!c->n || load_play(ch, c->slot[p->item]) != NX_OK)
        p->id = 0;
    else
        nx_timer_goes_on();   /* the chain goes on: nothing has "all finished" */
}

void nx_chain_task(uint32_t now)
{
    (void)now;
    for (unsigned ch = 0; ch < 2u; ch++) {
        if (!s_run[ch].p.id)
            continue;
        const struct channel *c = &g_ch[ch];
        if (g_ui_state == ST_PC || c->prog_slot != s_run[ch].slot) {
            s_run[ch].p.id = 0;   /* PC mode or another program */
            continue;
        }
        if (s_run[ch].next_due) {
            s_run[ch].next_due = 0;
            advance(ch);
        } else if (!(g_ui_flags & UIF_RUN(ch)) && c->elapsed <= 1u) {
            s_run[ch].p.id = 0;   /* stopped (STOP, its timer) */
        }
    }
    if (s_save_due && g_ui_state != ST_PC && nx_runner_quiet(nx_kv_free() < 256u ? 1000u : 100u)) {
#if NX_FEAT_PLANS
        if (s_save_due & 2u)
            nx_kv_put(NX_KEY_PLANS, &g_nx_plans, sizeof g_nx_plans);
        if (s_save_due & 1u)
#endif
            nx_kv_put(NX_KEY_CHAINS, &g_nx_chains, sizeof g_nx_chains);
        s_save_due = 0;
    }
}
#else
void nx_chain_boot(void)
{
}

void nx_chain_finished(unsigned ch)
{
    (void)ch;
}

void nx_chain_task(uint32_t now)
{
    (void)now;
}

void nx_chain_pos(unsigned ch, struct nx_chain_pos *p)
{
    (void)ch;
    memset(p, 0, sizeof *p);
}

void nx_chain_resume(unsigned ch, const struct nx_chain_pos *p)
{
    (void)ch;
    (void)p;
}
#endif
