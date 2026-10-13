/* nx runner and core actions (src/nx/nx_runner.c, nx_core.c) against the classic runner
 * (src/classic/runner.c) and the classic key actions (src/classic/app.c): the same random
 * programs and key sequences, applied to two copies of the firmware state, must produce the same
 * generator lines per link, in the same order, and the same runner state. Time is normalised: the
 * classic counts 10.1 ms units, nx milliseconds, so the test advances both by the same number of
 * seconds (in multiples of 50 ms, which never lands on the one-unit offset of the "stopped"
 * marker 1). */
#include "harness.h"

#include "app.h"
#include "link_internal.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_runner.h"
#include "nx_txq.h"
#include "nx_world.h"
#include "state.h"
#include "storage.h"
#include "test_nx.h"

#define SLOTS 6u
#define RUN_LOOP (UIF_RUN_G1 | UIF_RUN_G2 | UIF_LOOP_G1 | UIF_LOOP_G2)

struct fw_state {
    struct channel ch[2];
    uint16_t flags;   /* RUN/LOOP bits of g_ui_flags */
};

static uint32_t rng = 1;

static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

static uint64_t rnd_freq(void)
{
    switch (rnd(4)) {
    case 0:
        return 0;
    case 1:
        return UINT64_MAX;
    case 2:
        return (uint64_t)rnd(1000000) * 10u + rnd(10);
    default:
        return ((uint64_t)rnd(0xFFFFFFFFu) << 20) | rnd(1u << 20);
    }
}

static void random_program(struct program *p, uint16_t *params)
{
    memset(p, 0, sizeof *p);
    p->hdr[HDR_WAVE] = rnd(3) ? 41u + rnd(8) : rnd_freq();
    p->hdr[HDR_AMPL] = rnd(3) ? 2000u : rnd(100000);
    p->hdr[HDR_OFFS] = rnd(2) ? 120u : rnd(1000);
    p->hdr[HDR_DWELL] = rnd(4);                       /* 0 counts as 1 s */
    p->hdr[HDR_COUNT] = rnd(8) ? 1u + rnd(4) : 1u + rnd(PROGRAM_STEPS);
    for (unsigned i = 0; i < PROGRAM_STEPS; i++)
        p->freq[i] = rnd_freq();
    for (unsigned i = 0; i < 2u * PROGRAM_STEPS; i++) {
        unsigned k = rnd(10);
        params[i] = k < 3 ? 0xFFFFu : k < 5 ? 0 : (uint16_t)rnd(100000);
    }
}

static void store_slot(unsigned slot)
{
    struct program p;
    uint16_t params[2 * PROGRAM_STEPS];
    random_program(&p, params);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
}

static void save(struct fw_state *s)
{
    memcpy(s->ch, g_ch, sizeof s->ch);
    s->flags = g_ui_flags & RUN_LOOP;
}

static void load(const struct fw_state *s)
{
    memcpy(g_ch, s->ch, sizeof s->ch);
    g_ui_flags = (uint16_t)((g_ui_flags & ~RUN_LOOP) | s->flags);
}

/* ---- the classic side: app.c's key actions, runner.c ---- */

enum ev { EV_ADVANCE, EV_START, EV_STOP, EV_LOOP, EV_POWER, EV_LOAD, EV_UPLOAD };

static void classic_key(enum ev e, unsigned ch, unsigned slot)
{
    struct channel *c = &g_ch[ch];
    switch (e) {
    case EV_LOAD:     /* list_keys START */
        g_ch[0].step_sent = g_ch[1].step_sent = 0xFF;
        flags_clear(&g_ui_flags, UIF_RUN(ch));
        c->elapsed = 0;
        program_load(slot, ch);
        c->prog_slot = (uint8_t)slot;
        break;
    case EV_START:    /* channel_keys START */
        c->step_sent = 0xFF;
        if (!(g_ui_flags & UIF_RUN(ch))) {
            flags_set(&g_ui_flags, UIF_RUN(ch));
            flags_clear(&g_ui_flags, UIF_LOOP(ch));
        } else {
            flags_clear(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        }
        break;
    case EV_STOP:
        c->step_sent = 0xFF;
        c->elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        break;
    case EV_POWER:    /* short POWER: stop both */
        g_ch[0].step_sent = g_ch[1].step_sent = 0xFF;
        g_ch[0].elapsed = g_ch[1].elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN_G1 | UIF_RUN_G2);
        break;
    case EV_LOOP:
        c->step_sent = 0xFF;
        if (!(g_ui_flags & UIF_RUN(ch))) {
            flags_set(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        } else {
            flags_clear(&g_ui_flags, UIF_RUN(ch));
            flags_set(&g_ui_flags, UIF_LOOP(ch));
        }
        break;
    default:
        break;
    }
}

/* ---- the nx side: the same keys as nx_core actions ---- */

static void nx_key(enum ev e, unsigned ch, unsigned slot)
{
    struct nx_action a = { 0, (uint8_t)ch, (uint16_t)slot, 0, 0 };
    int running = (g_ui_flags & UIF_RUN(ch)) != 0;
    switch (e) {
    case EV_LOAD:
        a.type = NXA_LOAD;
        break;
    case EV_START:
        a.type = running ? NXA_PAUSE : NXA_PLAY;
        break;
    case EV_STOP:
        a.type = NXA_STOP;
        break;
    case EV_POWER:
        a.type = NXA_STOP_ALL;
        break;
    case EV_LOOP:
        a.type = running ? NXA_PAUSE : NXA_PLAY;
        a.v = 1;
        break;
    default:
        return;
    }
    CHECK_EQ(nx_core_act(&a), NX_OK);
}

static void drain(void)
{
    for (int i = 0; i < 100000 && !(nx_txq_idle(0) && nx_txq_idle(1)); i++) {
        nx_txq_service();
        nxw_poll();
        sim_advance_us(20);
        nxw_poll();
    }
}

static int same_lines;

/* Runs each side until it sends nothing more; compares what each link got and the state. */
static void run_and_compare(struct fw_state *cs, struct fw_state *ns)
{
    static char classic[2][1 << 14];
    static size_t classic_len[2];

    load(cs);
    nxw_tx_model(0);
    auto_ack = 1;
    tx_clear();
    for (int i = 0; i < 4; i++)
        runner_tick();
    for (unsigned p = 0; p < 2u; p++) {
        memcpy(classic[p], tx_log[p], tx_len[p]);
        classic_len[p] = tx_len[p];
    }
    save(cs);

    load(ns);
    auto_ack = 0;
    nxw_tx_model(1);
    tx_clear();
    for (int i = 0; i < 4; i++) {
        nx_runner_task();
        drain();
    }
    save(ns);

    int ok = 1;
    for (unsigned p = 0; p < 2u; p++)
        ok &= classic_len[p] == tx_len[p] && memcmp(classic[p], tx_log[p], tx_len[p]) == 0;
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct channel *a = &cs->ch[ch], *b = &ns->ch[ch];
        ok &= memcmp(&a->prog, &b->prog, sizeof a->prog) == 0;
        ok &= memcmp(a->params, b->params, sizeof a->params) == 0;
        ok &= a->step == b->step && a->step_sent == b->step_sent && a->step_freq == b->step_freq;
        ok &= a->prog_slot == b->prog_slot;
    }
    ok &= cs->flags == ns->flags;
    CHECK(ok);
    if (!ok) {
        printf("  differs (seed state): classic %zu/%zu bytes, nx %zu/%zu bytes\n", classic_len[0],
               classic_len[1], tx_len[0], tx_len[1]);
    }
    same_lines += ok;
}

static void differential(unsigned scenarios, unsigned events)
{
    same_lines = 0;
    unsigned total = 0;
    for (unsigned sc = 0; sc < scenarios; sc++) {
        nxw_reset();
        sim_on_ms(0);                         /* elapsed changes only through the test's events */
        g_ui_state = ST_HOME;
        nx_core_update_ready(NX_GEN_READY_MS);
        for (unsigned slot = 1; slot <= SLOTS; slot++)
            store_slot(slot);
        struct fw_state cs, ns;
        save(&cs);
        ns = cs;
        run_and_compare(&cs, &ns);            /* the boot idle configuration of both links */
        total++;
        for (unsigned k = 0; k < events; k++) {
            unsigned r = rnd(100), ch = rnd(2), slot = 1u + rnd(SLOTS);
            enum ev e = r < 40 ? EV_ADVANCE : r < 55 ? EV_START : r < 63 ? EV_STOP : r < 73 ? EV_LOOP
                        : r < 77 ? EV_POWER : r < 92 ? EV_LOAD : EV_UPLOAD;
            if (e == EV_ADVANCE) {
                uint32_t cs_units = 5u * (1u + rnd(60));   /* 50 ms .. 3 s */
                for (unsigned c = 0; c < 2u; c++) {
                    if (cs.flags & (c ? UIF_RUN_G2 : UIF_RUN_G1))
                        cs.ch[c].elapsed += cs_units;
                    if (ns.flags & (c ? UIF_RUN_G2 : UIF_RUN_G1))
                        ns.ch[c].elapsed += cs_units * 10u;
                }
            } else if (e == EV_UPLOAD) {          /* :g/:p into the channel's buffers (link.c) */
                struct program p;
                uint16_t params[2 * PROGRAM_STEPS];
                random_program(&p, params);
                cs.ch[ch].prog = ns.ch[ch].prog = p;
                memcpy(cs.ch[ch].params, params, sizeof params);
                memcpy(ns.ch[ch].params, params, sizeof params);
                cs.ch[ch].step_sent = ns.ch[ch].step_sent = 0xFF;
            } else {
                load(&cs);
                classic_key(e, ch, slot);
                save(&cs);
                load(&ns);
                nx_key(e, ch, slot);
                save(&ns);
            }
            run_and_compare(&cs, &ns);
            total++;
        }
    }
    printf("test_nx: runner differential: %d of %u steps identical\n", same_lines, total);
}

void test_nx_runner(void)
{
    rng = 20260924u;
    differential(500, 20);                    /* 10 000 key/time events on random programs */

    /* exact dwell: step 2 of a 1 s program is sent 1000 ms after START (the original: 1010) */
    nxw_reset();
    g_ui_state = ST_HOME;
    nx_core_update_ready(NX_GEN_READY_MS);
    struct program *p = &g_ch[0].prog;
    memset(p, 0, sizeof *p);
    p->hdr[HDR_OFFS] = 120;
    p->hdr[HDR_DWELL] = 1;
    p->hdr[HDR_COUNT] = 3;
    p->freq[0] = 11;
    p->freq[1] = 22;
    p->freq[2] = 33;
    memset(g_ch[0].params, 0, sizeof g_ch[0].params);
    g_ch[0].prog_slot = 9;
    g_ch[0].step_sent = 0xFF;
    g_ch[0].elapsed = 0;
    struct nx_action play = { NXA_PLAY, 0, 0, 0, 0 };
    for (int i = 0; i < 800; i++) {           /* 40 ms: the idle configuration of step 1 */
        nx_runner_task();
        nx_txq_service();
        nxw_poll();
        sim_advance_us(50);
    }
    nxw_clear();
    CHECK_EQ(nx_core_act(&play), NX_OK);
    uint64_t t0 = sim_us;                     /* a millisecond boundary */
    for (int i = 0; i < 70000; i++) {         /* 3.5 s: three steps, the end, the idle configuration */
        nx_runner_task();
        nx_txq_service();
        nxw_poll();
        sim_advance_us(50);
    }
    char lines[64][32];
    unsigned n = nxw_lines(0, lines, 64), k = 0;
    uint64_t w24[8];
    unsigned at[8];
    for (unsigned i = 0; i < n && k < 8u; i++)
        if (strncmp(lines[i], ":w24=", 5) == 0) {
            at[k] = i;
            w24[k++] = nxw_line_us[0][i] - t0;
        }
    CHECK_EQ(k, 4);
    CHECK(w24[0] < 1000u);
    CHECK(w24[1] >= 1000000u && w24[1] < 1001000u);
    CHECK(w24[2] >= 2000000u && w24[2] < 2001000u);
    CHECK(w24[3] >= 3000000u && w24[3] < 3010000u);
    CHECK(strcmp(lines[at[1]], ":w24=22,") == 0 && strcmp(lines[at[2]], ":w24=33,") == 0);
    CHECK(at[3] >= 1u && strcmp(lines[at[3] - 1u], ":w11=0,0,") == 0);   /* the end: outputs off */
    CHECK(strcmp(lines[at[3]], ":w24=11,") == 0);                       /* then idle, step 1 */
    CHECK_EQ(g_ui_flags & UIF_RUN_G1, 0);

    /* DEVIATION: a count above 200 ends after step 200 */
    nxw_reset();
    sim_on_ms(0);
    g_ui_state = ST_HOME;
    g_ch[1].prog.hdr[HDR_COUNT] = 300;
    g_ch[1].prog.hdr[HDR_DWELL] = 1;
    g_ch[1].step_sent = 200;
    g_ch[1].elapsed = 200u * NX_DWELL_UNITS;   /* step 201 */
    g_ui_flags |= UIF_RUN_G2;
    nx_runner_task();
    drain();
    CHECK_EQ(g_ui_flags & UIF_RUN_G2, 0);
    CHECK_EQ(g_ch[1].elapsed, 1);
    CHECK(tx_len[1] == 11 && memcmp(tx_log[1], ":w11=0,0,\r\n", 11) == 0);

    /* the runner is quiet in PC mode, in calibration mode and while a PC-mode change waits */
    nxw_reset();
    sim_on_ms(0);
    g_ch[0].step_sent = 0;
    g_ui_state = ST_PC;
    nx_runner_task();
    g_ui_state = ST_CAL;
    nx_runner_task();
    CHECK(nx_txq_idle(0) && nx_txq_idle(1));
    CHECK_EQ(g_ch[0].step_sent, 0);

    /* actions: refused before the boot gate, in PC mode, in calibration mode; bad arguments */
    nxw_reset();
    struct nx_action load = { NXA_LOAD, 0, 4, 0, 0 };
    g_ui_state = ST_HOME;
    CHECK_EQ(nx_core_act(&load), NX_BUSY);
    nx_core_update_ready(NX_GEN_READY_MS);
    CHECK_EQ(nx_core_act(&load), NX_OK);
    CHECK_EQ(g_ch[0].prog_slot, 4);
    g_ui_state = ST_PC;
    CHECK_EQ(nx_core_act(&load), NX_REFUSED_PC);
    g_ui_state = ST_CAL;
    CHECK_EQ(nx_core_act(&load), NX_BUSY);
    g_ui_state = ST_HOME;
    load.id = 31;
    CHECK_EQ(nx_core_act(&load), NX_EINVAL);
    load.id = 3;
    load.ch = 2;
    CHECK_EQ(nx_core_act(&load), NX_EINVAL);
    struct nx_action loop_on = { NXA_LOOP, 1, 0, 1, 0 };
    g_ch[1].step_sent = 5;
    CHECK_EQ(nx_core_act(&loop_on), NX_OK);   /* nx's loop toggle: a flag, nothing to send */
    CHECK(g_ui_flags & UIF_LOOP_G2);
    CHECK_EQ(g_ch[1].step_sent, 5);
}
