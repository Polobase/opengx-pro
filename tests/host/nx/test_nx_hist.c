/* History and usage totals (src/nx/nx_hist.h) on the host board model: a record per run with its
 * end (stopped, finished, timer, PC mode, power cut), short runs dropped, the totals, the ring
 * found again after a power-on, its wrap, a record torn by a power cut, no write in PC mode. */
#include "harness.h"

#include "link_internal.h"
#include "nx_chain.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_diag.h"
#include "nx_hist.h"
#include "nx_kv.h"
#include "nx_resume.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_ui.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#if NX_FEAT_HIST
static uint8_t s_flash[FLASH_SIZE];

/* steps of dwell_s seconds, count of them */
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
        p.freq[i] = 10000007u + 10u * i;
    uint16_t params[2 * PROGRAM_STEPS];
    memset(params, 0xFF, sizeof params);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
    memcpy(&g_names[slot][0], "Test program", 13);
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
    nx_hist_boot();
    nx_resume_boot();
}

static int act(uint8_t type, unsigned ch, unsigned id, uint32_t v)
{
    struct nx_action a = { type, (uint8_t)ch, (uint16_t)id, v, 0 };
    return nx_core_act(&a);
}

static void run_s(unsigned s)
{
    for (unsigned i = 0; i < s * 1000u; i++)
        nxw_step(1000);
}

void test_nx_hist(void)
{
    struct nx_hist_rec r;
    power_on(0);
    store_program(4, 3, 60);   /* 3 min */
    store_program(6, 2, 3);    /* 6 s */
    nxw_run_until(4600);
    CHECK_EQ(nx_hist_count(), 0);

    /* G1 plays slot 4 for 10 s (with a 5 s pause), STOP: one record, "stopped" */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    run_s(6);
    act(NXA_PAUSE, 0, 0, 0);
    run_s(5);
    act(NXA_PLAY, 0, 0, 0);
    run_s(4);
    act(NXA_STOP, 0, 0, 0);
    run_s(1);
    CHECK_EQ(nx_hist_pending(), 0);
    CHECK_EQ(nx_hist_count(), 1);
    CHECK(nx_hist_get(0, &r));
    CHECK(r.ch == 0 && r.slot == 4 && r.end == NX_HEND_STOPPED && r.session == 1 && r.seq == 1);
    CHECK(r.run_s >= 9u && r.run_s <= 10u);
    CHECK(r.start_s >= 4u && r.start_s <= 5u);
    CHECK(strncmp(r.name, "Test program", 13) == 0);
    const struct nx_totals *t = nx_hist_totals();
    CHECK(t->runs == 1 && t->sessions == 1 && t->slot_runs[3] == 1 && t->run_s == r.run_s);

    /* a run under NX_HIST_MIN_S is not kept */
    act(NXA_PLAY, 0, 0, 0);
    run_s(2);
    act(NXA_STOP, 0, 0, 0);
    run_s(1);
    CHECK_EQ(nx_hist_count(), 1);

    /* G2 plays slot 6 (6 s) to its end: "finished" */
    act(NXA_LOAD, 1, 6, 0);
    act(NXA_PLAY, 1, 0, 0);
    run_s(8);
    CHECK_EQ(nx_hist_count(), 2);
    CHECK(nx_hist_get(0, &r));
    CHECK(r.ch == 1 && r.slot == 6 && r.end == NX_HEND_FINISHED && r.seq == 2);
    CHECK(nx_hist_get(1, &r) && r.slot == 4);

#if NX_FEAT_TIMERS
    /* its "stop after" time: "timer" */
    g_nx_settings.session_limit_min[0] = 1;
    act(NXA_PLAY, 0, 0, 1);
    run_s(62);
    CHECK(nx_hist_get(0, &r) && r.end == NX_HEND_TIMER && r.run_s >= 59u && r.run_s <= 61u);
    g_nx_settings.session_limit_min[0] = 0;
#endif
    unsigned n = nx_hist_count();

    /* PC mode ends a run ("PC mode"); nothing is written while PB1 is high */
    act(NXA_PLAY, 0, 0, 0);
    run_s(8);
    nxw_pb1 = 1;
    run_s(3);
    CHECK_EQ(nx_hist_count(), n);
    CHECK_EQ(nx_hist_pending(), 1);
    nxw_pb1 = 0;
    run_s(3);
    CHECK_EQ(nx_hist_count(), n + 1u);
    CHECK(nx_hist_get(0, &r) && r.end == NX_HEND_PC);

    /* power-on: the ring is found again, a new session; a power cut while G1 played becomes a
     * "power cut" record from the resume snapshot */
    run_s(1);
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    run_s(70);   /* the snapshot is written every minute */
    power_on(1);
    CHECK_EQ(nx_hist_count(), n + 1u);
    CHECK_EQ(nx_hist_pending(), 1);
    nxw_run_until(5000);
    CHECK_EQ(nx_hist_count(), n + 2u);
    CHECK(nx_hist_get(0, &r) && r.end == NX_HEND_CUT && r.slot == 4 && r.session == 2);
    CHECK(r.run_s >= 60u);
    CHECK_EQ(nx_hist_totals()->sessions, 2);

    /* a record torn by a power cut: skipped, the next one after it */
    uint32_t seq = r.seq;
    power_on(1);
    unsigned pos = (seq - 1u) % NX_HIST_N;
    flash_mem[NX_HIST_START + pos * NX_HIST_REC_BYTES + 20u] ^= 0x55u;   /* inside its data */
    power_on(1);
    CHECK(!nx_hist_get(0, &r));
    nx_hist_add(0, 4, 0, 30, NX_HEND_STOPPED);   /* after the snapshot's "power cut" record again */
    nxw_run_until(5000);
    CHECK(nx_hist_get(0, &r) && r.run_s == 30u && r.seq == seq + 2u);
    CHECK(nx_hist_get(1, &r) && r.end == NX_HEND_CUT && r.seq == seq + 1u);
    CHECK(!nx_hist_get(2, &r));

    /* a chain of the same slot twice: each program its own run, the second one stopped */
#if NX_FEAT_CHAINS
    static const uint8_t k_same[2] = { 6, 6 };
    g_nx_chains.c[3].n = 2;
    g_nx_chains.c[3].repeat = 1;
    memcpy(g_nx_chains.c[3].slot, k_same, 2);
    CHECK_EQ(nx_chain_play(0, 4), NX_OK);
    run_s(11);   /* 6 s, then 5.7 s of the second (runs under NX_HIST_MIN_S are not kept) */
    nxw_run_ms(800);
    struct nx_action st = { NXA_STOP, 0, 0, 0, 0 };
    nx_core_act(&st);
    run_s(2);
    CHECK(nx_hist_get(0, &r) && r.slot == 6 && r.end == NX_HEND_STOPPED && r.run_s == 5u);
    CHECK(nx_hist_get(1, &r) && r.slot == 6 && r.end == NX_HEND_FINISHED && r.run_s >= 5u);
    memset(&g_nx_chains, 0, sizeof g_nx_chains);
#endif

    /* switched off (POWER held) while it plays: the run is recorded before the goodbye */
    act(NXA_LOAD, 0, 4, 0);
    act(NXA_PLAY, 0, 0, 0);
    run_s(12);
    unsigned before = nx_hist_count();
    volatile int off = 0;
    nxw_power_off_armed = 1;
    if (setjmp(nxw_power_off_jmp) == 0) {
        struct nx_action po = { NXA_POWER_OFF, 0, 0, 0, 0 };
        nx_core_act(&po);
        nxw_run_ms(5000);
    } else {
        off = 1;
    }
    nxw_power_off_armed = 0;
    CHECK(off);
    power_on(1);
    CHECK_EQ(nx_hist_count(), before + 1u);
    CHECK(nx_hist_get(0, &r) && r.slot == 4 && r.end == NX_HEND_STOPPED && r.run_s >= 11u);
    CHECK_EQ(nx_hist_pending(), 0);   /* no "power cut": the snapshot went with the power-off */
    nxw_run_until(4600);

    /* the ring comes round: the oldest sector goes, the newest are kept, also after a power-on */
    for (unsigned i = 0; i < NX_HIST_N + 10u; i++) {
        nx_hist_add(1, 7, i, 10, NX_HEND_FINISHED);
        nxw_step(1000);
    }
    run_s(1);
    CHECK_EQ(nx_hist_pending(), 0);
    CHECK_EQ(nx_hist_count(), NX_HIST_N - NX_HIST_PER_SECTOR);
    CHECK(nx_hist_get(0, &r) && r.start_s == NX_HIST_N + 9u);
    uint32_t newest = r.seq;
    CHECK(nx_hist_get(nx_hist_count() - 1u, &r) && r.seq == newest - (NX_HIST_N - NX_HIST_PER_SECTOR) + 1u);
    power_on(1);
    CHECK_EQ(nx_hist_count(), NX_HIST_N - NX_HIST_PER_SECTOR);
    CHECK(nx_hist_get(0, &r) && r.seq == newest);

    /* a sector's first record half erased by a power cut (a huge sequence number, a bad CRC): the
     * head is still found after the newest valid record */
    unsigned head_pos = newest % NX_HIST_N;   /* where the next record goes */
    unsigned bad = (head_pos / NX_HIST_PER_SECTOR + 1u) % NX_HIST_SECTORS * NX_HIST_PER_SECTOR;
    uint32_t huge = 0x7FFFFFFFu;
    memcpy(&flash_mem[NX_HIST_START + bad * NX_HIST_REC_BYTES], &huge, 4);
    power_on(1);
    CHECK(nx_hist_get(0, &r) && r.seq == newest);
    nx_hist_add(0, 4, 0, 20, NX_HEND_STOPPED);
    nxw_run_until(5000);
    CHECK(nx_hist_get(0, &r) && r.seq == newest + 1u && r.run_s == 20u);
    nx_settings_defaults(&g_nx_settings);
}
#else
void test_nx_hist(void)
{
}
#endif
