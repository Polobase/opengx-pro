/* nx storage v2: region map and superblock (docs/nx/design/storage-runner-features.md §1.2,
 * §1.4). Two superblock copies (A at 0x080000, B at 0x081000) describe the layout; the one with
 * the higher sequence number wins. Mounting only reads. A region that is still erased is
 * formatted on the first write (B, then A); a copy torn by a power cut during that format starts
 * with the superblock magic and is formatted again. Any other data in the region without a valid
 * superblock (a future vendor firmware?) makes the storage read-only, as does a layout that needs
 * a newer firmware (min_reader_ver).
 *
 * Implemented here: the superblock and the KV store (nx_kv.h); the journal, history and object
 * heap regions are only reserved in the superblock. */
#pragma once

#include <stdint.h>

#define NX_SB_A 0x080000u
#define NX_SB_B 0x081000u
#define NX_KV_START 0x082000u
#define NX_KV_SECTORS 8u          /* two pages of 4 sectors */
#define NX_JRNL_START 0x090000u
#define NX_JRNL_SECTORS 16u
#define NX_HIST_START 0x0A0000u
#define NX_HIST_SECTORS 32u
#define NX_HEAP_START 0x0C0000u
#define NX_HEAP_SECTORS 320u
#define NX_SB_MAGIC 0x4253584Eu   /* "NXSB" */
#define NX_LAYOUT_VER 1u

struct nx_super {                 /* at NX_SB_A and NX_SB_B */
    uint32_t magic;               /* NX_SB_MAGIC */
    uint16_t layout_ver;          /* NX_LAYOUT_VER */
    uint16_t min_reader_ver;      /* oldest nx layout version allowed to write this layout */
    uint32_t seq;                 /* the higher valid copy wins */
    uint32_t kv_start, kv_sectors, jrnl_start, jrnl_sectors, hist_start, hist_sectors, heap_start, heap_sectors;
    uint32_t created_by_fw;       /* NX_FW_ID of the firmware that formatted */
    uint32_t format_count;
    uint8_t mcu_uid[12];          /* informational: the flash chip moved to another board */
    uint32_t crc32;               /* nx_crc32 of all bytes before it */
};

enum nx_store_state {
    NX_STORE_UNMOUNTED = 0,
    NX_STORE_EMPTY,     /* erased (or our format was cut): formatted on the first write */
    NX_STORE_OK,
    NX_STORE_READONLY,  /* foreign data or a newer layout: never written */
};

void nx_store_reset(void);            /* RAM state back to unmounted (boot, host tests) */
int nx_store_mount(void);             /* reads the superblocks and the KV store; returns the state */
enum nx_store_state nx_store_state(void);
const struct nx_super *nx_store_super(void);   /* NULL unless NX_STORE_OK */
/* Before an nx write: formats an empty region. NX_OK, NX_EREADONLY, NX_REFUSED_PC, NX_EFLASH. */
int nx_store_prepare_write(void);
