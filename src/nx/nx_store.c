/* nx storage v2: superblock and mount (nx_store.h). */
#include "nx_store.h"

#include <stddef.h>
#include <string.h>

#include "nx_config.h"
#include "nx_crc.h"
#include "nx_flash.h"
#include "nx_hw.h"
#include "nx_kv.h"
#include "state.h"

_Static_assert(sizeof(struct nx_super) == 68, "superblock layout");

static enum nx_store_state s_state;
static struct nx_super s_sb;

void nx_store_reset(void)
{
    s_state = NX_STORE_UNMOUNTED;
    memset(&s_sb, 0, sizeof s_sb);
    nx_kv_reset();
}

static uint32_t sb_crc(const struct nx_super *sb)
{
    return nx_crc32(0, sb, (uint32_t)offsetof(struct nx_super, crc32));
}

static int sb_read(uint32_t addr, struct nx_super *sb)
{
    nx_flash_read(addr, sb, sizeof *sb);
    return sb->magic == NX_SB_MAGIC && sb->crc32 == sb_crc(sb);
}

/* Erased, or a copy of our own format that a power cut interrupted (programs start with the
 * magic, so a torn copy still has it). */
static int ours_or_erased(uint32_t addr)
{
    uint32_t magic;
    nx_flash_read(addr, &magic, sizeof magic);
    return magic == NX_SB_MAGIC || nx_flash_is_erased(addr, NX_FLASH_SECTOR);
}

int nx_store_mount(void)
{
    struct nx_super a, b;
    int va = sb_read(NX_SB_A, &a), vb = sb_read(NX_SB_B, &b);
    nx_kv_reset();
    if (va || vb) {
        s_sb = (va && (!vb || a.seq >= b.seq)) ? a : b;
        if (s_sb.min_reader_ver > NX_LAYOUT_VER) {
            s_state = NX_STORE_READONLY;
        } else {
            s_state = NX_STORE_OK;
            nx_kv_mount();
        }
        return s_state;
    }
    if (ours_or_erased(NX_SB_A) && ours_or_erased(NX_SB_B)
        && nx_flash_is_erased(NX_KV_START, NX_KV_SECTORS * NX_FLASH_SECTOR))
        s_state = NX_STORE_EMPTY;
    else
        s_state = NX_STORE_READONLY;   /* not ours: never write it (only an explicit erase later) */
    return s_state;
}

enum nx_store_state nx_store_state(void)
{
    return s_state;
}

const struct nx_super *nx_store_super(void)
{
    return s_state == NX_STORE_OK ? &s_sb : NULL;
}

static int write_copy(uint32_t addr, const struct nx_super *sb)
{
    if (!nx_flash_is_erased(addr, NX_FLASH_SECTOR)) {
        int rc = nx_flash_erase(addr);
        if (rc != NX_OK)
            return rc;
    }
    return nx_flash_program(addr, sb, sizeof *sb);
}

static int format(void)
{
    struct nx_super sb;
    memset(&sb, 0, sizeof sb);
    sb.magic = NX_SB_MAGIC;
    sb.layout_ver = NX_LAYOUT_VER;
    sb.min_reader_ver = NX_LAYOUT_VER;
    sb.seq = 1;
    sb.kv_start = NX_KV_START;
    sb.kv_sectors = NX_KV_SECTORS;
    sb.jrnl_start = NX_JRNL_START;
    sb.jrnl_sectors = NX_JRNL_SECTORS;
    sb.hist_start = NX_HIST_START;
    sb.hist_sectors = NX_HIST_SECTORS;
    sb.heap_start = NX_HEAP_START;
    sb.heap_sectors = NX_HEAP_SECTORS;
    sb.created_by_fw = NX_FW_ID;
    sb.format_count = 1;
    nx_hw_uid(sb.mcu_uid);
    sb.crc32 = sb_crc(&sb);
    int rc = write_copy(NX_SB_B, &sb);   /* B first: a cut before A leaves B valid */
    if (rc == NX_OK)
        rc = write_copy(NX_SB_A, &sb);
    if (rc == NX_OK)
        s_sb = sb;
    return rc;
}

int nx_store_prepare_write(void)
{
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    if (s_state == NX_STORE_OK)
        return NX_OK;
    if (s_state != NX_STORE_EMPTY)
        return NX_EREADONLY;
    int rc = format();
    if (rc == NX_OK)
        s_state = NX_STORE_OK;
    return rc;
}
