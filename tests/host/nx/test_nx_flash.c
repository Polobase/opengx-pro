/* CRC-32 and the guarded SPI-flash layer (src/nx/nx_crc.c, nx_flash.c). */
#include "harness.h"

#include "fmt.h"
#include "nx_config.h"
#include "nx_crc.h"
#include "nx_diag.h"
#include "nx_flash.h"
#include "nx_world.h"
#include "spiflash.h"
#include "state.h"
#include "test_nx.h"

/* The STM32 CRC unit as RM0454 describes it, for the device path of nx_crc.c: an MSB-first
 * CRC-32 register (polynomial 0x04C11DB7) loaded from INIT, input bytes bit-reversed (REV_IN =
 * byte), the register read out bit-reversed (REV_OUT). */
static uint32_t rbit32(uint32_t v)
{
    uint32_t r = 0;
    for (int i = 0; i < 32; i++)
        r |= ((v >> i) & 1u) << (31 - i);
    return r;
}

static uint32_t unit_crc(uint32_t init, const uint8_t *p, uint32_t len)
{
    uint32_t reg = init;
    while (len--) {
        uint8_t b = *p++, rev = 0;
        for (int i = 0; i < 8; i++)
            rev |= (uint8_t)(((b >> i) & 1u) << (7 - i));
        reg ^= (uint32_t)rev << 24;
        for (int k = 0; k < 8; k++)
            reg = (reg & 0x80000000u) ? (reg << 1) ^ 0x04C11DB7u : reg << 1;
    }
    return rbit32(reg);
}

void test_nx_flash(void)
{
    /* zlib values, chaining in chunks */
    CHECK_EQ(nx_crc32(0, "123456789", 9), 0xCBF43926u);
    CHECK_EQ(nx_crc32(nx_crc32(0, "1234", 4), "56789", 5), 0xCBF43926u);
    CHECK_EQ(nx_crc32(0, "", 0), 0);
    CHECK_EQ(nx_crc32(0, "a", 1), 0xE8B7BE43u);
    /* the device path's arithmetic (INIT = rbit(~crc), result ~DR) equals zlib, also chained */
    uint8_t data[64];
    uint32_t seed = 7;
    int unit_ok = 1;
    for (int round = 0; round < 200; round++) {
        for (unsigned i = 0; i < sizeof data; i++) {
            seed = seed * 1103515245u + 12345u;
            data[i] = (uint8_t)(seed >> 16);
        }
        uint32_t len = seed % 64u, prev = seed;
        unit_ok &= ~unit_crc(rbit32(~prev), data, len) == fmt_crc32(prev, data, len);
    }
    CHECK(unit_ok);

    /* program across a page boundary, read back, erase check */
    nxw_reset();
    g_ui_state = ST_HOME;
    uint8_t buf[300];
    for (unsigned i = 0; i < sizeof buf; i++)
        buf[i] = (uint8_t)(i * 7u + 1u);
    CHECK_EQ(nx_flash_erase(0x090123), NX_OK);   /* the sector containing the address */
    CHECK_EQ(nx_flash_program(0x0900F0, buf, sizeof buf), NX_OK);
    CHECK(memcmp(&flash_mem[0x0900F0], buf, sizeof buf) == 0);
    CHECK(!nx_flash_is_erased(0x090000, 0x1000));
    CHECK(nx_flash_is_erased(0x091000, 0x1000));
    uint8_t back[8];
    CHECK_EQ(nx_flash_read(0x0900F0, back, sizeof back), NX_OK);
    CHECK(memcmp(back, buf, sizeof back) == 0);
    CHECK_EQ(flash_op_count, 4);                 /* one erase, three page programs (16 + 256 + 28) */

    /* the guard: nx never writes outside 0x080000-0x1FFFFF, the attempt is counted */
    flash_guard(NX_FLASH_LO, NX_FLASH_HI);
    CHECK_EQ(nx_flash_erase(0x074000), NX_EGUARD);
    CHECK_EQ(nx_flash_program(0x07FFF0, buf, 32), NX_EGUARD);
    CHECK_EQ(nx_flash_program(0x1FFFF0, buf, 32), NX_EGUARD);
    CHECK_EQ(nx_flash_erase(0x200000), NX_EGUARD);
    CHECK_EQ(g_nx_diag.flash_guard_refusals, 4);
    CHECK_EQ(flash_op_count, 4);
    CHECK_EQ(flash_guard_violations, 0);

    /* no nx writes in PC mode */
    g_ui_state = ST_PC;
    CHECK_EQ(nx_flash_erase(0x090000), NX_REFUSED_PC);
    CHECK_EQ(nx_flash_program(0x091000, buf, 4), NX_REFUSED_PC);
    CHECK_EQ(flash_op_count, 4);
    g_ui_state = ST_HOME;

    /* read-back: data that did not reach the array is reported */
    CHECK_EQ(nx_flash_program(0x0900F0, buf + 1, 16), NX_EFLASH);   /* over programmed bytes */
    CHECK_EQ(g_nx_diag.flash_verify_errors, 1);
    flash_cut_at(0, FLASH_CUT_CLEAN);                                /* power gone: nothing written */
    CHECK_EQ(nx_flash_program(0x092000, buf, 16), NX_EFLASH);
    CHECK_EQ(g_nx_diag.flash_verify_errors, 2);
    flash_power_on();

    /* SPIFLASH_ACQUIRE: a legacy read (storage.c path) waits for an operation still running */
    flash_timing_us(1);
    spiflash_erase_begin(0x093000);
    uint64_t t0 = sim_us;
    unsigned violations = flash_violations;
    uint8_t name[4];
    spiflash_read(0x005000, name, sizeof name);
    CHECK(sim_us - t0 >= FLASH_T_SE_US);
    CHECK_EQ(g_nx_diag.flash_quiesce_waits, 1);
    CHECK_EQ(flash_violations, violations);
    /* SPIFLASH_POLL_HOOK: blocking waits feed the watchdog */
    unsigned feeds = nxw_wdg_feeds;
    CHECK_EQ(nx_flash_erase(0x094000), NX_OK);
    CHECK(nxw_wdg_feeds > feeds + 100u);
}
