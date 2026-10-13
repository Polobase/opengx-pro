/* nx key-value store (docs/nx/design/storage-runner-features.md §1.5): an append-only log in two
 * pages of 16 KB (4 sectors each) at NX_KV_START. Power-cut safe by construction:
 *   - a page is used only when its header is valid and its ACTIVE word, programmed last, is
 *     exactly NX_KV_ACTIVE; of two such pages the higher generation wins;
 *   - an entry is a 16-byte header (key, length, sequence, value CRC, header CRC) followed by the
 *     value, padded to 16 bytes; a torn value fails its CRC and is skipped (the key keeps its old
 *     value), a torn header ends the trusted part of the page (it counts as full);
 *   - when the active page is full, compaction erases the other page, writes its header, copies
 *     the latest value of every key plus the new one, and programs ACTIVE last: a cut at any point
 *     leaves the old page in charge.
 * Mount scans the active page into a RAM index (key -> offset). Length 0 deletes a key. Writes go
 * through nx_store_prepare_write() (formats an empty region; refused when read-only or in PC
 * mode). */
#pragma once

#include <stdint.h>

#define NX_KV_PAGE_BYTES 0x4000u    /* 4 sectors */
#define NX_KV_HDR_BYTES 32u
#define NX_KV_ENT_BYTES 16u
#define NX_KV_MAX_KEYS 64u
#define NX_KV_VAL_MAX 1024u
#define NX_KV_PAGE_MAGIC 0x564B584Eu   /* "NXKV" */
#define NX_KV_ACTIVE 0x56544341u       /* "ACTV", programmed last */
#define NX_KV_ENT_MAGIC 0x4B56u

struct nx_kv_page_hdr {        /* first 32 bytes of a page */
    uint32_t magic;            /* NX_KV_PAGE_MAGIC */
    uint32_t gen;              /* +1 per compaction */
    uint32_t hcrc;             /* nx_crc32 of magic and gen */
    uint32_t rsv[4];           /* 0xFFFFFFFF */
    uint32_t active;           /* 0xFFFFFFFF until the page is complete, then NX_KV_ACTIVE */
};

struct nx_kv_ent {             /* 16 bytes, then the value padded to 16 bytes */
    uint16_t magic;            /* NX_KV_ENT_MAGIC */
    uint16_t key;
    uint16_t len;              /* 0 = key deleted */
    uint16_t hcrc16;           /* low 16 bits of nx_crc32(magic, key, len, seq, vcrc32) */
    uint32_t seq;              /* global write counter */
    uint32_t vcrc32;           /* nx_crc32 of the value */
};

void nx_kv_reset(void);                 /* RAM index empty, no active page */
int nx_kv_mount(void);                  /* scan the active page; returns the number of keys */
/* Copies up to cap bytes of the value; returns its full length, or NX_ENOTFOUND / NX_EFLASH. */
int nx_kv_get(uint16_t key, void *buf, uint16_t cap);
/* NX_OK, NX_ENOSPACE (value > NX_KV_VAL_MAX, too many keys, page full of live data),
 * NX_EREADONLY, NX_REFUSED_PC, NX_EFLASH. */
int nx_kv_put(uint16_t key, const void *val, uint16_t len);
int nx_kv_del(uint16_t key);
unsigned nx_kv_count(void);
uint32_t nx_kv_free(void);              /* bytes left in the active page */
int nx_kv_active_page(void);            /* 0 or 1, -1 = none yet */
