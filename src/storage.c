/* Persistent data in the external SPI NOR flash, byte-identical to the original layout
 * (docs/spec/hardware.md §6.1, application.md §6.1, §10, validated against the user's backup in
 * application.md §13):
 *
 *   0x005000            names, 32 x 60 bytes (slots 0 and 31 always stored as zeros)
 *   0x010000 + n*0x1000 program slot n = 1..30: record (u64 hdr[5] + u64 freq[200], 0x668 bytes)
 *                       at +0, step parameters (u16[400], 800 bytes) at +0x668
 *   0x074000            calibration table, 32 x s16 (valid if word 31 == 31)
 *   0x075000            calibration backup ("factory") copy
 *
 * All multi-byte values are little endian, i.e. the RAM image of the Cortex-M0+. This is the
 * only module that erases or programs the SPI flash. */
#include "storage.h"

#include <string.h>

#include "link_internal.h"
#include "spiflash.h"
#include "state.h"

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the flash records are stored as raw little-endian RAM images"
#endif
_Static_assert(sizeof(struct program) == PROG_RECORD_BYTES, "program record must be 0x668 bytes");
_Static_assert(sizeof(g_ch[0].params) == PROG_PARAMS_BYTES, "step parameters must be 800 bytes");
_Static_assert(sizeof(g_names) == NAMES_BYTES, "name table must be 32 x 60 bytes");
_Static_assert(sizeof(g_cal_table) == CAL_BYTES, "calibration table must be 64 bytes");

static const uint8_t k_zero_name[NAME_LEN];

void storage_init(void)
{
    /* Nothing to set up: spiflash_init() brings up SPI1, the loaders are called by the app. */
}

/* ---- names (0x5000) ---- */

void names_load(void)   /* store_names_load 0x0800E29C */
{
    uint8_t *p = (uint8_t *)g_names;
    spiflash_read(NAMES_ADDR, p, NAMES_BYTES);
    for (unsigned i = 0; i < NAMES_BYTES; i++)
        if (p[i] == 0xFF)
            p[i] = 0;
}

void names_save(void)   /* store_names_save 0x08011380 */
{
    const uint32_t slot31 = NAMES_ADDR + (NAME_SLOTS - 1u) * NAME_LEN;
    if (spiflash_sector_erase(NAMES_ADDR))
        return;
    spiflash_write(NAMES_ADDR, k_zero_name, NAME_LEN);
    spiflash_write(NAMES_ADDR + NAME_LEN, (const uint8_t *)g_names[1], (NAME_SLOTS - 2u) * NAME_LEN);
    spiflash_write(slot31, k_zero_name, NAME_LEN);
}

void name_set(unsigned slot, const char *text, unsigned len)   /* 0x080124D8 */
{
    if (slot >= NAME_SLOTS)
        return;
    char *dst = g_names[slot];
    unsigned i = 0;
    if (len > NAME_LEN)
        len = NAME_LEN;
    for (; i < len && text[i]; i++)
        dst[i] = text[i];
    for (; i < NAME_LEN; i++)
        dst[i] = 0;
}

/* ---- programs (0x10000 + n*0x1000) ---- */

static int slot_valid(unsigned slot)
{
    return slot >= 1u && slot <= PROGRAM_SLOTS;
}

void storage_slot_read(unsigned slot, uint32_t offset, void *buf, uint32_t len)
{
    spiflash_read(PROG_SLOT_ADDR(slot) + offset, (uint8_t *)buf, len);
}

void program_load(unsigned slot, unsigned ch)   /* store_program_load 0x0800E2E4 */
{
    if (ch > 1u)
        return;
    struct channel *c = &g_ch[ch];
    uint64_t hdr[5];
    storage_slot_read(slot, 0, hdr, sizeof hdr);
    if (hdr[HDR_COUNT] == 0 || hdr[HDR_COUNT] > PROGRAM_STEPS || hdr[HDR_DWELL] == 0) {
        memset(&c->prog, 0, sizeof c->prog);   /* defaults of the original */
        c->prog.hdr[HDR_OFFS] = 120;
        c->prog.hdr[HDR_DWELL] = 1;
        c->prog.hdr[HDR_COUNT] = 1;
    } else {
        storage_slot_read(slot, 0, &c->prog, PROG_RECORD_BYTES);
    }
    storage_slot_read(slot, PROG_PARAMS_OFFSET, c->params, PROG_PARAMS_BYTES);
}

void program_save(unsigned slot, unsigned ch)   /* store_program_save 0x080113EC */
{
    /* DEVIATION: slots outside 1..30 are refused (the original computes the address of any
     * slot number; its callers only pass 1..30). */
    if (ch > 1u || !slot_valid(slot))
        return;
    const uint32_t addr = PROG_SLOT_ADDR(slot);
    if (spiflash_sector_erase(addr))
        return;
    spiflash_write(addr, (const uint8_t *)&g_ch[ch].prog, PROG_RECORD_BYTES);
    spiflash_write(addr + PROG_PARAMS_OFFSET, (const uint8_t *)g_ch[ch].params, PROG_PARAMS_BYTES);
}

void program_erase(unsigned slot)
{
    /* DEVIATION: ':n00=,' erases nothing here; the original erases the unused sector of
     * "slot" 0 (0x10000) on USART1 or 31 (0x2F000) on USART2. */
    if (slot_valid(slot))
        spiflash_sector_erase(PROG_SLOT_ADDR(slot));
}

void storage_erase_all(void)   /* w96 path of 0x0800F42C / 0x0800FBBE */
{
    for (unsigned slot = 1; slot <= PROGRAM_SLOTS; slot++)
        spiflash_sector_erase(PROG_SLOT_ADDR(slot));
    memset(g_names, 0, sizeof g_names);
    spiflash_sector_erase(NAMES_ADDR);
}

/* ---- calibration table (0x74000, backup 0x75000) ---- */

static int cal_differs(uint32_t addr)
{
    int16_t stored[32];
    spiflash_read(addr, (uint8_t *)stored, CAL_BYTES);
    return memcmp(stored, g_cal_table, CAL_BYTES) != 0;
}

static void cal_write(uint32_t addr)
{
    if (spiflash_sector_erase(addr) == 0)
        spiflash_write(addr, (const uint8_t *)g_cal_table, CAL_BYTES);
}

void cal_load(void)   /* store_settings_load 0x0800E264 */
{
    int16_t stored[32];
    spiflash_read(CAL_ADDR, (uint8_t *)stored, CAL_BYTES);
    if (stored[31] == 31)
        memcpy(g_cal_table, stored, CAL_BYTES);
}

void cal_save(int backup)   /* store_settings_save 0x08011310 */
{
    if (cal_differs(CAL_ADDR))
        cal_write(CAL_ADDR);
    /* DEVIATION: the backup copy is compared on its own. The original writes it only when the
     * working copy changed, so a save to backup right after a normal save was lost. */
    if (backup && cal_differs(CAL_BACKUP_ADDR))
        cal_write(CAL_BACKUP_ADDR);
}

void cal_restore_backup(void)   /* store_settings_restore_backup 0x08011288 */
{
    int16_t stored[32];
    spiflash_read(CAL_BACKUP_ADDR, (uint8_t *)stored, CAL_BYTES);
    if ((uint16_t)stored[0] == 0xFFFFu)
        return;
    memcpy(g_cal_table, stored, CAL_BYTES);
    if (cal_differs(CAL_ADDR))
        cal_write(CAL_ADDR);
}
