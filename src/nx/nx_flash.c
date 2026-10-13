/* Guarded SPI-flash access (nx_flash.h). */
#include "nx_flash.h"

#include <string.h>

#include "nx_config.h"
#include "nx_diag.h"
#include "nx_time.h"
#include "nx_wdg.h"
#include "spiflash.h"
#include "state.h"

#define CHUNK 64u

static int inside(uint32_t addr, uint32_t len)
{
    if (addr >= NX_FLASH_LO && addr < NX_FLASH_HI && len <= NX_FLASH_HI - addr)
        return 1;
    g_nx_diag.flash_guard_refusals++;
    return 0;
}

int nx_flash_read(uint32_t addr, void *buf, uint32_t len)
{
    spiflash_read(addr, (uint8_t *)buf, len);
    return NX_OK;
}

int nx_flash_is_erased(uint32_t addr, uint32_t len)
{
    uint8_t chunk[CHUNK];
    while (len) {
        uint32_t n = len < CHUNK ? len : CHUNK;
        spiflash_read(addr, chunk, n);
        for (uint32_t i = 0; i < n; i++)
            if (chunk[i] != 0xFFu)
                return 0;
        addr += n;
        len -= n;
    }
    return 1;
}

int nx_flash_erase(uint32_t addr)
{
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    addr &= ~(NX_FLASH_SECTOR - 1u);
    if (!inside(addr, NX_FLASH_SECTOR))
        return NX_EGUARD;
    if (spiflash_sector_erase(addr)) {
        g_nx_diag.flash_timeouts++;
        return NX_EFLASH;
    }
    if (!nx_flash_is_erased(addr, NX_FLASH_SECTOR)) {
        g_nx_diag.flash_verify_errors++;
        return NX_EFLASH;
    }
    return NX_OK;
}

int nx_flash_program(uint32_t addr, const void *buf, uint32_t len)
{
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    if (!inside(addr, len))
        return NX_EGUARD;
    if (spiflash_write(addr, (const uint8_t *)buf, len)) {
        g_nx_diag.flash_timeouts++;
        return NX_EFLASH;
    }
    const uint8_t *p = (const uint8_t *)buf;
    uint8_t chunk[CHUNK];
    while (len) {
        uint32_t n = len < CHUNK ? len : CHUNK;
        spiflash_read(addr, chunk, n);
        if (memcmp(chunk, p, n) != 0) {
            g_nx_diag.flash_verify_errors++;
            return NX_EFLASH;
        }
        addr += n;
        p += n;
        len -= n;
    }
    return NX_OK;
}

void nx_flash_poll(void)
{
    nx_wdg_feed();
}

void nx_flash_acquire(void)
{
    if (!spiflash_busy())
        return;
    g_nx_diag.flash_quiesce_waits++;
    uint32_t t0 = nx_ms();
    while (spiflash_busy()) {
        if (nx_ms() - t0 > NX_FLASH_ACQUIRE_MS) {
            g_nx_diag.flash_timeouts++;
            return;
        }
        nx_wdg_feed();
    }
}
