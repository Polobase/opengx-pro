/* Chains (src/nx/nx_chain.h) on the host board model: the programs one after the other, the
 * repeat count, what ends a chain (STOP, another program, PC mode), the chains kept in the KV
 * store, a power cut in the middle (resumed with its position), and no "all finished" power-off
 * offer between two programs of a chain. */
#include "harness.h"

#include "link_internal.h"
#include "nx_chain.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_kv.h"
#include "nx_resume.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_keys.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#if NX_FEAT_CHAINS
static uint8_t s_flash[FLASH_SIZE];

static void store_program(unsigned slot, unsigned count, unsigned dwell_s)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 11;
    p.hdr[HDR_AMPL] = 1000;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = dwell_s;
    p.hdr[HDR_COUNT] = count;
    for (unsigned i = 0; i < count; i++)
        p.freq[i] = 10000007u + 1000u * slot + 10u * i;
    uint16_t params[2 * PROGRAM_STEPS];
    memset(params, 0xFF, sizeof params);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
}

static void power_on(int keep_flash)
{
    if (keep_flash)
        memcpy(s_flash, flash_mem, FLASH_SIZE);
    nxw_reset();
    if (keep_flash)
        memcpy(flash_mem, s_flash, FLASH_SIZE);
    nx_store_mount();
    nx_settings_load(&g_nx_settings);
    nx_chain_boot();
    nx_resume_boot();
}

static void run_s(unsigned s)
{
    for (unsigned i = 0; i < s * 1000u; i++)
        nxw_step(1000);
}

static void set_chain(unsigned id, uint8_t repeat, unsigned n, const uint8_t *slots)
{
    struct nx_chain *c = &g_nx_chains.c[id - 1u];
    c->n = (uint8_t)n;
    c->repeat = repeat;
    memcpy(c->slot, slots, n);
}

static void press(uint8_t keys, uint32_t hold_ms)
{
    nxw_press(keys, hold_ms);
    nxw_run_ms(250);
}

static unsigned chain_id(unsigned ch)
{
    struct nx_chain_pos p;
    nx_chain_pos(ch, &p);
    return p.id;
}

void test_nx_chain(void)
{
    static const uint8_t k_abc[3] = { 4, 6, 5 };
    power_on(0);
    store_program(4, 2, 3);   /* each about 6 s */
    store_program(5, 2, 3);
    store_program(6, 2, 3);
    nxw_run_until(4600);
    set_chain(1, 1, 3, k_abc);
    CHECK_EQ(nx_chain_play(0, 2), NX_EINVAL);   /* empty */

    /* 4, 6, 5 on G1, once */
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    CHECK(g_ch[0].prog_slot == 4 && (g_ui_flags & UIF_RUN_G1) && !(g_ui_flags & UIF_LOOP_G1));
    run_s(5);
    CHECK_EQ(g_ch[0].prog_slot, 4);
    run_s(2);
    CHECK(g_ch[0].prog_slot == 6 && (g_ui_flags & UIF_RUN_G1));
    run_s(6);
    CHECK(g_ch[0].prog_slot == 5 && (g_ui_flags & UIF_RUN_G1));
    run_s(7);
    CHECK(g_ch[0].prog_slot == 5 && !(g_ui_flags & UIF_RUN_G1));   /* done */
    CHECK_EQ(chain_id(0), 0);

    /* twice, on G2 */
    set_chain(2, 2, 2, k_abc);
    CHECK_EQ(nx_chain_play(1, 2), NX_OK);
    run_s(13);
    CHECK_EQ(g_ch[1].prog_slot, 4);   /* the second run */
    CHECK_EQ(chain_id(1), 2);
    run_s(13);
    CHECK(!(g_ui_flags & UIF_RUN_G2) && chain_id(1) == 0);

    /* STOP, another program, PC mode end it */
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(2);
    struct nx_action a = { NXA_STOP, 0, 0, 0, 0 };
    nx_core_act(&a);
    run_s(1);
    CHECK_EQ(chain_id(0), 0);
    run_s(8);
    CHECK(g_ch[0].prog_slot == 4 && !(g_ui_flags & UIF_RUN_G1));
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(2);
    a = (struct nx_action){ NXA_LOAD, 0, 5, 0, 0 };
    nx_core_act(&a);
    run_s(1);
    CHECK_EQ(chain_id(0), 0);
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(2);
    nxw_pb1 = 1;
    run_s(2);
    CHECK_EQ(chain_id(0), 0);
    nxw_pb1 = 0;
    run_s(2);

    /* kept in the KV store */
    nx_chain_save();
    run_s(1);
    struct nx_chains_rec r;
    CHECK(nx_kv_get(NX_KEY_CHAINS, &r, sizeof r) == (int)sizeof r);
    CHECK(r.c[0].n == 3 && r.c[0].slot[1] == 6 && r.c[1].repeat == 2);

    /* a power cut in the second program: resumed, it goes on with the third */
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(10);   /* slot 6, written to the snapshot */
    CHECK_EQ(g_ch[0].prog_slot, 6);
    power_on(1);
    CHECK(g_nx_chains.c[0].n == 3);
    CHECK_EQ(nx_resume_offer(), 1);
    nxw_run_until(4600);
    CHECK_EQ(nx_resume_accept(), NX_OK);
    CHECK(g_ch[0].prog_slot == 6 && chain_id(0) == 1);
    run_s(6);
    CHECK(g_ch[0].prog_slot == 5 && (g_ui_flags & UIF_RUN_G1));
    run_s(7);
    CHECK(!(g_ui_flags & UIF_RUN_G1));

#if NX_FEAT_TIMERS
    /* "power off when all finished": not between the programs, 15 s after the last */
    g_nx_settings.off_when_done = 1;
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(30);   /* the chain ended at about 18 s */
    CHECK(!nx_core_power_off_due());
    volatile int off = 0;
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0)
        run_s(6);
    else
        off = 1;
    nxw_power_off_armed = 0;
    CHECK(off);
#endif

    /* a chain goes on: its STOP later is no "all finished" (no power-off card) */
    power_on(1);
    nxw_run_until(4600);
    g_nx_settings.off_when_done = 1;
    CHECK_EQ(nx_chain_play(0, 1), NX_OK);
    run_s(8);   /* the second program */
    CHECK_EQ(g_ch[0].prog_slot, 6);
    struct nx_action stp = { NXA_STOP, 0, 0, 0, 0 };
    nx_core_act(&stp);
    run_s(20);
    CHECK(!nx_core_power_off_due());
    g_nx_settings.off_when_done = 0;

#if NX_FEAT_PLANS
    /* a plan of three days: chain 1, chain 2, chain 1; a day moves on only when its chain has
     * run to its end, is kept in the KV store, survives a power cut, and the complete plan plays
     * nothing */
    g_nx_settings.off_when_done = 0;
    power_on(1);   /* after the power-off above */
    nxw_run_until(4600);
    struct nx_plan *pl = &g_nx_plans.p[0];
    pl->n = 3;
    pl->day = 0;
    pl->chain[0] = 1;
    pl->chain[1] = 2;
    pl->chain[2] = 1;
    CHECK_EQ(nx_plan_play(0, 2), NX_EINVAL);   /* empty */
    CHECK_EQ(nx_plan_play(0, 1), NX_OK);        /* day 1: chain 1 (4, 6, 5), about 18 s */
    run_s(5);
    struct nx_action st = { NXA_STOP, 0, 0, 0, 0 };
    nx_core_act(&st);                           /* stopped: the day does not count */
    run_s(1);
    CHECK_EQ(pl->day, 0);
    CHECK_EQ(nx_plan_play(0, 1), NX_OK);
    run_s(20);
    CHECK_EQ(pl->day, 1);
    run_s(1);
    struct nx_plans_rec pr;
    CHECK(nx_kv_get(NX_KEY_PLANS, &pr, sizeof pr) == (int)sizeof pr && pr.p[0].day == 1 && pr.p[0].n == 3);
    CHECK_EQ(nx_plan_play(1, 1), NX_OK);        /* day 2: chain 2 (4, 6) twice, on G2 */
    CHECK_EQ(g_ch[1].prog_slot, 4);
    run_s(8);                                   /* in its second program: a power cut */
    power_on(1);
    CHECK_EQ(g_nx_plans.p[0].day, 1);
    nxw_run_until(4600);
    CHECK_EQ(nx_resume_accept(), NX_OK);
    struct nx_chain_pos cp;
    nx_chain_pos(1, &cp);
    CHECK(cp.id == 2 && cp.plan == 1);
    run_s(20);
    CHECK(!(g_ui_flags & UIF_RUN_G2));
    CHECK_EQ(g_nx_plans.p[0].day, 2);
    CHECK_EQ(nx_plan_play(0, 1), NX_OK);        /* day 3 */
    run_s(20);
    CHECK_EQ(g_nx_plans.p[0].day, 3);
    CHECK_EQ(nx_plan_play(0, 1), NX_EINVAL);    /* complete */
    memset(&g_nx_plans, 0, sizeof g_nx_plans);
#endif

    /* the keys: menu > Chains > chain 3 > LOOP (edit) > START (add: slot 1) > G2 G2 (slot 3) >
     * START (a copy after it) > LOOP (x2) > START held on the second (removed) > STOP (saved) */
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.splash = NX_SPLASH_OFF;
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    power_on(1);
    nxw_run_until(4600);
    nx_scr_menu_preselect(&nx_scr_chains, 0);
    press(NX_KEY_DOWN, 100);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_chains);
    press(NX_KEY_DOWN, 100);
    press(NX_KEY_DOWN, 100);
    press(NX_KEY_LOOP, 100);
    CHECK(nx_ui_top() == &nx_scr_chain_edit);
    press(NX_KEY_START, 100);
    press(NX_KEY_G2, 100);
    press(NX_KEY_G2, 100);
    press(NX_KEY_START, 100);
    press(NX_KEY_G2, 100);   /* the copy: slot 4 */
    press(NX_KEY_LOOP, 100);
    const struct nx_chain *c = &g_nx_chains.c[2];
    CHECK(c->n == 2 && c->slot[0] == 3 && c->slot[1] == 4 && c->repeat == 2);
    press(NX_KEY_START, NX_KEY_HOLD_MS + 200u);
    CHECK(c->n == 1 && c->slot[0] == 3);
    press(NX_KEY_STOP, 100);
    CHECK(nx_ui_top() == &nx_scr_chains);
    run_s(1);
    struct nx_chains_rec r2;
    CHECK(nx_kv_get(NX_KEY_CHAINS, &r2, sizeof r2) == (int)sizeof r2 && r2.c[2].n == 1 && r2.c[2].slot[0] == 3);
    press(NX_KEY_START, 100);   /* plays it on the focused generator */
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK(g_ch[0].prog_slot == 3 && chain_id(0) == 3);

    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
    nx_settings_defaults(&g_nx_settings);
    memset(&g_nx_chains, 0, sizeof g_nx_chains);
}
#else
void test_nx_chain(void)
{
}
#endif
