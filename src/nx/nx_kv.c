/* nx key-value store (nx_kv.h). */
#include "nx_kv.h"

#include <string.h>

#include "nx_config.h"
#include "nx_crc.h"
#include "nx_flash.h"
#include "nx_store.h"

_Static_assert(sizeof(struct nx_kv_page_hdr) == NX_KV_HDR_BYTES, "page header layout");
_Static_assert(sizeof(struct nx_kv_ent) == NX_KV_ENT_BYTES, "entry header layout");

#define CHUNK 64u

struct kv_idx {
    uint16_t key, len, off;   /* off: header of the latest entry, within the active page */
};

static struct kv_idx s_idx[NX_KV_MAX_KEYS];
static unsigned s_n;
static int s_page = -1;
static uint32_t s_gen, s_seq = 1;
static uint32_t s_end;        /* next free offset; NX_KV_PAGE_BYTES when full or untrusted */

static uint32_t page_addr(int p)
{
    return NX_KV_START + (uint32_t)p * NX_KV_PAGE_BYTES;
}

static uint32_t padded(uint32_t len)
{
    return (len + 15u) & ~15u;
}

static uint16_t ent_hcrc(const struct nx_kv_ent *e)
{
    uint32_t crc = nx_crc32(0, e, 6);   /* magic, key, len */
    return (uint16_t)nx_crc32(crc, &e->seq, 8);
}

static uint32_t page_hcrc(const struct nx_kv_page_hdr *h)
{
    return nx_crc32(0, h, 8);           /* magic, gen */
}

void nx_kv_reset(void)
{
    s_n = 0;
    s_page = -1;
    s_gen = 0;
    s_seq = 1;
    s_end = NX_KV_PAGE_BYTES;
}

static int find(uint16_t key)
{
    for (unsigned i = 0; i < s_n; i++)
        if (s_idx[i].key == key)
            return (int)i;
    return -1;
}

static void apply(uint16_t key, uint16_t len, uint32_t off)
{
    int i = find(key);
    if (len == 0) {
        if (i >= 0)
            s_idx[i] = s_idx[--s_n];
        return;
    }
    if (i < 0) {
        if (s_n >= NX_KV_MAX_KEYS)
            return;
        i = (int)s_n++;
    }
    s_idx[i].key = key;
    s_idx[i].len = len;
    s_idx[i].off = (uint16_t)off;
}

static int value_ok(uint32_t addr, uint32_t len, uint32_t vcrc)
{
    uint8_t chunk[CHUNK];
    uint32_t crc = 0;
    while (len) {
        uint32_t n = len < CHUNK ? len : CHUNK;
        nx_flash_read(addr, chunk, n);
        crc = nx_crc32(crc, chunk, n);
        addr += n;
        len -= n;
    }
    return crc == vcrc;
}

static int erased16(const struct nx_kv_ent *e)
{
    const uint8_t *p = (const uint8_t *)e;
    for (unsigned i = 0; i < sizeof *e; i++)
        if (p[i] != 0xFFu)
            return 0;
    return 1;
}

static void scan(int p)
{
    uint32_t base = page_addr(p), off = NX_KV_HDR_BYTES;
    while (off + NX_KV_ENT_BYTES <= NX_KV_PAGE_BYTES) {
        struct nx_kv_ent e;
        nx_flash_read(base + off, &e, sizeof e);
        if (erased16(&e))
            break;
        if (e.magic != NX_KV_ENT_MAGIC || e.hcrc16 != ent_hcrc(&e) || e.len > NX_KV_VAL_MAX
            || off + NX_KV_ENT_BYTES + padded(e.len) > NX_KV_PAGE_BYTES) {
            off = NX_KV_PAGE_BYTES;   /* torn header: nothing after it can be trusted */
            break;
        }
        if (value_ok(base + off + NX_KV_ENT_BYTES, e.len, e.vcrc32))
            apply(e.key, e.len, off);   /* a torn value is skipped: the key keeps its old value */
        if (e.seq >= s_seq)
            s_seq = e.seq + 1u;
        off += NX_KV_ENT_BYTES + padded(e.len);
    }
    s_end = off;
}

int nx_kv_mount(void)
{
    nx_kv_reset();
    for (int p = 0; p < 2; p++) {
        struct nx_kv_page_hdr h;
        nx_flash_read(page_addr(p), &h, sizeof h);
        if (h.magic != NX_KV_PAGE_MAGIC || h.hcrc != page_hcrc(&h) || h.active != NX_KV_ACTIVE)
            continue;
        if (s_page < 0 || h.gen > s_gen) {
            s_page = p;
            s_gen = h.gen;
        }
    }
    if (s_page >= 0)
        scan(s_page);
    return (int)s_n;
}

int nx_kv_get(uint16_t key, void *buf, uint16_t cap)
{
    int i = find(key);
    if (i < 0 || s_page < 0)
        return NX_ENOTFOUND;
    uint32_t addr = page_addr(s_page) + s_idx[i].off;
    uint16_t len = s_idx[i].len;
    uint16_t n = len < cap ? len : cap;
    nx_flash_read(addr + NX_KV_ENT_BYTES, buf, n);
    if (n == len) {   /* whole value read: check it again (retention) */
        struct nx_kv_ent e;
        nx_flash_read(addr, &e, sizeof e);
        if (nx_crc32(0, buf, n) != e.vcrc32)
            return NX_EFLASH;
    }
    return len;
}

static int append(int p, uint32_t off, uint16_t key, const void *val, uint16_t len, uint32_t seq)
{
    struct nx_kv_ent e;
    e.magic = NX_KV_ENT_MAGIC;
    e.key = key;
    e.len = len;
    e.seq = seq;
    e.vcrc32 = nx_crc32(0, val, len);
    e.hcrc16 = ent_hcrc(&e);
    uint32_t addr = page_addr(p) + off;
    int rc = nx_flash_program(addr, &e, sizeof e);   /* header first: a cut leaves a bad value CRC */
    if (rc == NX_OK && len)
        rc = nx_flash_program(addr + NX_KV_ENT_BYTES, val, len);
    return rc;
}

static int copy_entry(int from, uint32_t from_off, int to, uint32_t to_off, uint16_t len)
{
    uint8_t chunk[CHUNK];
    uint32_t src = page_addr(from) + from_off, dst = page_addr(to) + to_off;
    uint32_t left = NX_KV_ENT_BYTES + len;   /* header (unchanged: same seq and CRCs) + value */
    while (left) {
        uint32_t n = left < CHUNK ? left : CHUNK;
        nx_flash_read(src, chunk, n);
        int rc = nx_flash_program(dst, chunk, n);
        if (rc != NX_OK)
            return rc;
        src += n;
        dst += n;
        left -= n;
    }
    return NX_OK;
}

static int compact(uint16_t key, const void *val, uint16_t len)
{
    int dst = s_page < 0 ? 0 : 1 - s_page;
    uint32_t base = page_addr(dst);
    uint32_t total = NX_KV_HDR_BYTES + (len ? NX_KV_ENT_BYTES + padded(len) : 0);
    for (unsigned i = 0; i < s_n; i++)
        if (s_idx[i].key != key)
            total += NX_KV_ENT_BYTES + padded(s_idx[i].len);
    if (total > NX_KV_PAGE_BYTES)
        return NX_ENOSPACE;

    for (uint32_t s = 0; s < NX_KV_PAGE_BYTES; s += NX_FLASH_SECTOR) {   /* header sector first */
        if (!nx_flash_is_erased(base + s, NX_FLASH_SECTOR)) {
            int rc = nx_flash_erase(base + s);
            if (rc != NX_OK)
                return rc;
        }
    }
    struct nx_kv_page_hdr h;
    memset(&h, 0xFF, sizeof h);
    h.magic = NX_KV_PAGE_MAGIC;
    h.gen = s_page < 0 ? 1u : s_gen + 1u;
    h.hcrc = page_hcrc(&h);
    int rc = nx_flash_program(base, &h, sizeof h);   /* ACTIVE stays 0xFFFFFFFF for now */
    if (rc != NX_OK)
        return rc;

    uint16_t new_off[NX_KV_MAX_KEYS] = { 0 };
    uint32_t off = NX_KV_HDR_BYTES;
    for (unsigned i = 0; i < s_n; i++) {
        if (s_idx[i].key == key)
            continue;
        rc = copy_entry(s_page, s_idx[i].off, dst, off, s_idx[i].len);
        if (rc != NX_OK)
            return rc;
        new_off[i] = (uint16_t)off;
        off += NX_KV_ENT_BYTES + padded(s_idx[i].len);
    }
    uint32_t key_off = off;
    if (len) {
        rc = append(dst, off, key, val, len, s_seq);
        if (rc != NX_OK)
            return rc;
        off += NX_KV_ENT_BYTES + padded(len);
    }
    uint32_t active = NX_KV_ACTIVE;
    rc = nx_flash_program(base + (uint32_t)(sizeof h - sizeof active), &active, sizeof active);
    if (rc != NX_OK)
        return rc;

    /* The new page is in charge now. */
    for (unsigned i = 0; i < s_n; i++)
        if (s_idx[i].key != key)
            s_idx[i].off = new_off[i];
    s_page = dst;
    s_gen = h.gen;
    s_end = off;
    apply(key, len, key_off);
    s_seq++;
    return NX_OK;
}

int nx_kv_put(uint16_t key, const void *val, uint16_t len)
{
    if (len > NX_KV_VAL_MAX)
        return NX_ENOSPACE;
    int rc = nx_store_prepare_write();
    if (rc != NX_OK)
        return rc;
    int i = find(key);
    if (len == 0 && i < 0)
        return NX_OK;   /* deleting a missing key writes nothing */
    if (len && i < 0 && s_n >= NX_KV_MAX_KEYS)
        return NX_ENOSPACE;
    uint32_t need = NX_KV_ENT_BYTES + padded(len);
    if (s_page < 0 || s_end + need > NX_KV_PAGE_BYTES)
        return compact(key, val, len);
    rc = append(s_page, s_end, key, val, len, s_seq);
    if (rc != NX_OK) {
        s_end = NX_KV_PAGE_BYTES;   /* the page may hold a broken entry: compact on the next write */
        return rc;
    }
    apply(key, len, s_end);
    s_end += need;
    s_seq++;
    return NX_OK;
}

int nx_kv_del(uint16_t key)
{
    return nx_kv_put(key, 0, 0);
}

unsigned nx_kv_count(void)
{
    return s_n;
}

uint32_t nx_kv_free(void)
{
    return s_page < 0 ? NX_KV_PAGE_BYTES - NX_KV_HDR_BYTES : NX_KV_PAGE_BYTES - s_end;
}

int nx_kv_active_page(void)
{
    return s_page;
}
