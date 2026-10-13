/* Host tests of the nx firmware (make -C tests/host runs them after test_link): the shared
 * harness (mocks.c, the W25Q16 model, the simulated clock) and
 * the nx modules. This file holds main() and the self-tests of the harness extensions the nx
 * tests rely on. */
#include "harness.h"

#include "mock_board.h"
#include "spiflash.h"
#include "test_nx.h"

void nx_reset_world(void)
{
    mocks_reset();
    flash_reset();
    sim_reset();
}

/* ---- simulated clock ---- */

static unsigned ticks;

static void count_tick(void)
{
    ticks++;
}

static void test_simclock(void)
{
    nx_reset_world();
    CHECK_EQ(sim_ms(), 0);
    ticks = 0;
    sim_on_ms(count_tick);
    sim_advance_us(999);
    CHECK_EQ(ticks, 0);
    sim_advance_us(1);
    CHECK_EQ(ticks, 1);
    sim_advance_ms(10);
    CHECK_EQ(ticks, 11);
    CHECK_EQ(sim_ms(), 11);
    delay_loop(16000);                  /* 4 cycles per iteration at 64 MHz: 1 ms */
    CHECK_EQ(sim_us, 12000);
    CHECK_EQ(ticks, 12);
    sim_reset();
    sim_advance_ms(5);
    CHECK_EQ(ticks, 12);                /* sim_reset removed the tick hook */
}

/* ---- W25Q16 busy time in simulated microseconds ---- */

static void test_flash_timing_us(void)
{
    nx_reset_world();
    flash_timing_us(1);
    uint64_t t0 = sim_us;
    CHECK_EQ(spiflash_sector_erase(0x90000), 0);   /* blocking: polls until the erase is done */
    CHECK(sim_us - t0 >= FLASH_T_SE_US);
    CHECK(sim_us - t0 < FLASH_T_SE_US + 100u);
    CHECK_EQ(flash_violations, 0);

    spiflash_erase_begin(0x91000);                 /* non-blocking: busy for 45 ms of simulated time */
    CHECK_EQ(spiflash_busy(), 1);
    sim_advance_ms(44);
    CHECK_EQ(spiflash_busy(), 1);
    sim_advance_ms(2);
    CHECK_EQ(spiflash_busy(), 0);

    static const uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    t0 = sim_us;
    CHECK_EQ(spiflash_write(0x91000, data, sizeof data), 0);
    CHECK(sim_us - t0 >= FLASH_T_PP_US);
    CHECK_EQ(flash_mem[0x91007], 8);
    CHECK_EQ(flash_violations, 0);

    /* after flash_reset: busy for a number of status polls, as test_link expects */
    flash_reset();
    spiflash_erase_begin(0x1000);
    unsigned polls = 0;
    while (spiflash_busy())
        polls++;
    CHECK_EQ(polls, 5);
}

/* ---- power cuts, erase counters, address guard ---- */

static void fill_page(uint32_t a, uint8_t v)
{
    static uint8_t buf[256];
    memset(buf, v, sizeof buf);
    CHECK_EQ(spiflash_write(a, buf, sizeof buf), 0);
}

static void test_flash_cut(void)
{
    /* a clean cut before operation 1: operation 0 done, 1 and everything after it dropped */
    nx_reset_world();
    flash_cut_at(1, FLASH_CUT_CLEAN);
    CHECK_EQ(spiflash_sector_erase(0x80000), 0);
    fill_page(0x80000, 0x11);
    CHECK(flash_cut_happened());
    CHECK(flash_is_erased(0x80000, 0x1000));
    CHECK_EQ(flash_op_count, 2);
    CHECK_EQ(flash_erase_count(0x80000), 1);
    fill_page(0x80100, 0x22);                     /* no power: nothing happens, no violation */
    CHECK(flash_is_erased(0x80100, 0x100));
    CHECK_EQ(flash_violations, 0);
    flash_power_on();
    fill_page(0x80100, 0x22);
    CHECK_EQ(flash_mem[0x801FF], 0x22);

    /* a torn page program: the first half of the bytes reached the array */
    nx_reset_world();
    flash_cut_at(0, FLASH_CUT_TEAR_PAGE | FLASH_CUT_TEAR_ERASE);
    fill_page(0x80000, 0x00);
    CHECK(flash_cut_happened());
    CHECK_EQ(flash_mem[0x8007F], 0x00);
    CHECK_EQ(flash_mem[0x80080], 0xFF);
    CHECK_EQ(flash_programs, 1);

    /* a torn sector erase: the first 2 KB erased, the rest as before */
    nx_reset_world();
    memset(&flash_mem[0x81000], 0x00, 0x1000);
    flash_cut_at(0, FLASH_CUT_TEAR_ERASE);
    CHECK_EQ(spiflash_sector_erase(0x81000), 0);
    CHECK(flash_is_erased(0x81000, 0x800));
    CHECK_EQ(flash_mem[0x81800], 0x00);
    CHECK_EQ(flash_erase_count(0x81000), 1);

    /* a tear flag that does not fit the operation drops it, like a clean cut */
    nx_reset_world();
    flash_cut_at(0, FLASH_CUT_TEAR_ERASE);
    fill_page(0x80000, 0x00);
    CHECK(flash_cut_happened());
    CHECK(flash_is_erased(0x80000, 0x100));
}

/* The pattern the nx storage tests use: cut at every operation of a sequence in turn. */
static void cut_sequence(void)
{
    CHECK_EQ(spiflash_sector_erase(0x82000), 0);
    fill_page(0x82000, 0x5A);
    fill_page(0x82100, 0xA5);
}

static void test_flash_cut_sweep(void)
{
    nx_reset_world();
    cut_sequence();
    unsigned n = flash_op_count;
    CHECK_EQ(n, 3);
    unsigned cuts = 0;
    for (unsigned k = 0; k <= n; k++) {
        nx_reset_world();
        memset(&flash_mem[0x82000], 0x00, 0x1000);
        flash_cut_at(k, FLASH_CUT_TEAR_PAGE | FLASH_CUT_TEAR_ERASE);
        cut_sequence();
        if (!flash_cut_happened()) {
            CHECK_EQ(k, n);                       /* only past the last operation */
            CHECK_EQ(flash_mem[0x821FF], 0xA5);
            continue;
        }
        cuts++;
        CHECK_EQ(flash_op_count, k + 1);          /* nothing starts after the cut */
        CHECK_EQ(flash_mem[0x82FFF], k == 0 ? 0x00 : 0xFF);   /* a torn erase keeps the upper half */
    }
    CHECK_EQ(cuts, n);
}

static void test_flash_guard(void)
{
    nx_reset_world();
    flash_guard(0x080000, 0x200000);              /* the nx area */
    CHECK_EQ(spiflash_sector_erase(0x080000), 0);
    CHECK_EQ(flash_guard_violations, 0);
    CHECK_EQ(spiflash_sector_erase(0x074000), 0); /* the legacy calibration sector: outside */
    CHECK_EQ(flash_guard_violations, 1);
    fill_page(0x07FF00, 0x33);
    CHECK_EQ(flash_guard_violations, 2);
    CHECK_EQ(flash_erase_count(0x074000), 1);
    CHECK_EQ(flash_erase_count(0x075000), 0);
    CHECK_EQ(spiflash_sector_erase(0x080000), 0);
    CHECK_EQ(flash_erase_count(0x080FFF), 2);
}

int main(void)
{
    test_simclock();
    test_flash_timing_us();
    test_flash_cut();
    test_flash_cut_sweep();
    test_flash_guard();
    test_nx_time();
    test_nx_keys();
    test_nx_bl();
    test_nx_fault();
    test_nx_txq();
    test_nx_runner();
    test_nx_core();
    test_nx_flash();
    test_nx_store();
    test_nx_settings();
    test_nx_ui();
    test_nx_lcd();
    test_nx_gfx();
    test_nx_anim();
    test_nx_render();
    test_nx_trans();
    test_nx_hero();
    test_nx_scope();
    test_nx_uiset();
    test_nx_uicore();
    test_nx_comp();
    test_nx_ui_set();
    test_nx_resume();
    test_nx_hist();
    test_nx_slots();
    test_nx_chain();
    test_nx_terrain();
    test_nx_manual();
    test_nx_preset();
    test_nx_ui_engine();
    printf("test_nx: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
