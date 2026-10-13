/* The UI's view of the core (src/nx/nx_uicore.c) on the host board model: the snapshot from
 * g_ch[] and the runner's millisecond times, the PC-mode mirrors, names, slot headers and
 * frequencies from the SPI flash (not from g_ch), the settings blob (no write in PC mode) and the
 * panel variant. */
#include "harness.h"

#include "link_internal.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_runner.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_uicore.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

static void store(unsigned slot, unsigned count, unsigned dwell, uint64_t f0)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 12;
    p.hdr[HDR_AMPL] = 1500;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = dwell;
    p.hdr[HDR_COUNT] = count;
    for (unsigned i = 0; i < count; i++)
        p.freq[i] = f0 + i * 10u;
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
}

static void act(uint8_t type, unsigned ch, uint16_t id, uint32_t v)
{
    struct nx_action a = { type, (uint8_t)ch, id, v, 0 };
    CHECK_EQ(nx_uc_act(&a), NX_OK);
}

static void test_times(void)
{
    struct nx_run_times t;
    nxw_reset();
    store(3, 4, 30, 7278);
    nxw_run_until(4600);
    act(NXA_LOAD, 0, 3, 0);
    nx_runner_times(0, &t);
    CHECK_EQ(t.step, 1);
    CHECK_EQ(t.count, 4);
    CHECK_EQ(t.dwell_ms, 30000);
    CHECK_EQ(t.total_ms, 120000);
    CHECK_EQ(t.left_ms, 120000);
    CHECK_EQ(t.step_left_ms, 30000);
    g_ch[0].elapsed = 30000;   /* exactly the start of step 2 */
    nx_runner_times(0, &t);
    CHECK_EQ(t.step, 2);
    CHECK_EQ(t.step_left_ms, 30000);
    CHECK_EQ(t.left_ms, 90000);
    g_ch[0].elapsed = 119999;
    nx_runner_times(0, &t);
    CHECK_EQ(t.step, 4);
    CHECK_EQ(t.step_left_ms, 1);
    g_ch[0].elapsed = 500000;  /* past the end: clamped */
    nx_runner_times(0, &t);
    CHECK_EQ(t.step, 4);
    CHECK_EQ(t.left_ms, 0);
    CHECK_EQ(t.step_left_ms, 0);
    /* a dwell of 0 plays as 1 s */
    g_ch[1].prog.hdr[HDR_DWELL] = 0;
    g_ch[1].prog.hdr[HDR_COUNT] = 3;
    g_ch[1].elapsed = 0;
    nx_runner_times(1, &t);
    CHECK_EQ(t.dwell_ms, 1000);
    CHECK_EQ(t.total_ms, 3000);
}

static void test_snapshot(void)
{
    struct nx_vm vm;
    nxw_reset();
    store(3, 4, 30, 7278);
    nxw_run_until(4600);
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].state, NXG_NONE);
    CHECK_EQ(vm.g[0].slot, 0);

    act(NXA_LOAD, 0, 3, 0);
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].state, NXG_READY);
    CHECK_EQ(vm.g[0].slot, 3);
    CHECK_EQ(vm.g[0].count, 4);
    CHECK_EQ(vm.g[0].step, 1);
    CHECK_EQ(vm.g[0].wave, 12);
    CHECK_EQ(vm.g[0].ampl_cv, 1500);
    CHECK_EQ(vm.g[0].dwell_s, 30);
    CHECK_EQ(vm.g[0].total_s, 120);
    CHECK_EQ(vm.g[0].left_s, 120);
    CHECK_EQ(vm.g[0].freq, 7278);
    CHECK_EQ(vm.g[0].next, 7288);

    act(NXA_PLAY, 0, 0, 0);
    nxw_run_ms(45500);   /* into step 2 */
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].state, NXG_RUNNING);
    CHECK_EQ(vm.g[0].step, 2);
    CHECK_EQ(vm.g[0].freq, 7288);
    CHECK_EQ(vm.g[0].next, 7298);
    CHECK(vm.g[0].left_s >= 74 && vm.g[0].left_s <= 75);
    CHECK(vm.g[0].step_left_s >= 14 && vm.g[0].step_left_s <= 15);
    CHECK(vm.g[0].frac > 24000 && vm.g[0].frac < 25000);   /* 45.5 of 120 s */

    act(NXA_PAUSE, 0, 0, 1);
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].state, NXG_PAUSED);
    CHECK_EQ(vm.g[0].loop, 1);
    CHECK_EQ(vm.g[0].next, 7298);
    act(NXA_STOP, 0, 0, 0);
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].state, NXG_READY);
    CHECK_EQ(vm.g[0].step, 1);
    CHECK_EQ(vm.g[0].loop, 0);

    /* the last step's next frequency is the first one only when looping */
    g_ch[0].elapsed = 100000;
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].step, 4);
    CHECK_EQ(vm.g[0].next, 0);
    act(NXA_LOOP, 0, 0, 1);
    nx_uc_vm(&vm);
    CHECK_EQ(vm.g[0].next, 7278);

    /* PC mode mirrors */
    nxw_pb1 = 1;
    nxw_run_ms(300);
    g_ch[1].pc_freq[0] = 41234567893u;
    g_misc_flags = MISC_OUT_EN(1, 0) | MISC_OUT_EN(0, 1);
    g_ui_flags |= UIF_SYNC_G2;
    nx_uc_vm(&vm);
    CHECK_EQ(vm.s.pc, 1);
    CHECK_EQ(vm.s.pc_freq[1][0], 41234567893u);
    CHECK_EQ(vm.s.pc_out, 0x06);   /* G1 Out2, G2 Out1 */
    CHECK_EQ(vm.s.pc_sync, 2);
    /* the cycles of the wave table each output plays (0: not uploaded this session, or no table) */
    CHECK_EQ(vm.s.pc_mult[1][0], 0);
    g_nx_pcmon.cycles[1][0] = 11;                    /* table 11 */
    g_nx_pcmon.cycles[1][NX_PCMON_TABS - 1u] = 3;    /* table 26 */
    g_nx_pcmon.wave[1][0] = 11;
    g_nx_pcmon.wave[1][1] = 26;
    g_nx_pcmon.wave[0][0] = 27;                      /* not a kept table */
    g_nx_pcmon.wave[0][1] = 10;
    nx_uc_vm(&vm);
    CHECK_EQ(vm.s.pc_mult[1][0], 11);
    CHECK_EQ(vm.s.pc_mult[1][1], 3);
    CHECK_EQ(vm.s.pc_mult[0][0], 0);
    CHECK_EQ(vm.s.pc_mult[0][1], 0);
    /* the amplitudes as the PC sent them (peak to peak; Spooky2's own output pane shows the same) */
    g_nx_pcmon.ampl_cv[0][0] = 1000;
    g_nx_pcmon.ampl_cv[1][1] = 0xFFFFu;
    nx_uc_vm(&vm);
    CHECK_EQ(vm.s.pc_ampl[0][0], 1000);
    CHECK_EQ(vm.s.pc_ampl[1][1], 0xFFFFu);
    CHECK_EQ(nx_uc_act(&(struct nx_action){ NXA_PLAY, 0, 0, 0, 0 }), NX_REFUSED_PC);
}

static void test_names_and_slots(void)
{
    char name[NX_UC_NAME_MAX + 1u];
    struct nx_slot_info si;
    uint64_t f[4];
    nxw_reset();
    store(5, 3, 60, 43200000001u);
    memcpy(g_names[5], "Lyme \x81 test\x7F", 13);
    CHECK_EQ(nx_uc_name(5, name), 12);
    CHECK(strcmp(name, "Lyme ? test?") == 0);   /* bytes the fonts do not have */
    memset(g_names[6], 'A', NAME_LEN);           /* 60 characters, no terminator in g_names */
    CHECK_EQ(nx_uc_name(6, name), 60);
    CHECK_EQ(name[60], 0);
    CHECK_EQ(nx_uc_name(7, name), 0);
    CHECK_EQ(name[0], 0);

    CHECK_EQ(nx_uc_slot_info(5, &si), 1);
    CHECK_EQ(si.count, 3);
    CHECK_EQ(si.dwell_s, 60);
    CHECK_EQ(si.wave, 12);
    CHECK_EQ(nx_uc_slot_info(6, &si), 0);        /* erased */
    CHECK_EQ(nx_uc_slot_info(0, &si), 0);
    CHECK_EQ(nx_uc_slot_info(31, &si), 0);
    CHECK_EQ(nx_uc_slot_freqs(5, 1, f, 2), 2);
    CHECK_EQ(f[0], 43200000011u);
    CHECK_EQ(f[1], 43200000021u);
    CHECK_EQ(nx_uc_slot_freqs(5, 199, f, 4), 1);   /* the table ends at step 200 */
    /* g_ch is the upload buffer: the detail reads the flash, not g_ch */
    g_ch[0].prog.freq[0] = 999;
    CHECK_EQ(nx_uc_slot_freqs(5, 0, f, 1), 1);
    CHECK_EQ(f[0], 43200000001u);
}

static void test_settings_blob(void)
{
    nxw_reset();
    nx_store_mount();
    nx_settings_defaults(&g_nx_settings);
    nxw_run_until(4600);
    uint8_t *b = nx_uc_settings_blob();
    CHECK(b == g_nx_settings.ui);
    b[0] = 1;
    b[2] = 4;
    CHECK_EQ(nx_uc_settings_save(), NX_OK);
    struct nx_settings s;
    CHECK_EQ(nx_settings_load(&s), NX_OK);
    CHECK_EQ(s.ui[2], 4);
    /* PC mode: no nx-initiated SPI write */
    nxw_pb1 = 1;
    nxw_run_ms(300);
    b[2] = 5;
    CHECK_EQ(nx_uc_settings_save(), NX_REFUSED_PC);
    CHECK_EQ(nx_settings_load(&s), NX_OK);
    CHECK_EQ(s.ui[2], 4);
}

static void test_panel(void)
{
    nxw_reset();
    g_nx_diag.panel = 'B';   /* main_nx.c: lcd_init's variant */
    CHECK_EQ(nx_uc_panel(), 'B');
    g_nx_diag.panel = 0;     /* not detected (the host has no panel init) */
    CHECK_EQ(nx_uc_panel(), '?');
}

void test_nx_uicore(void)
{
    test_times();
    test_snapshot();
    test_names_and_slots();
    test_settings_blob();
    test_panel();
}
