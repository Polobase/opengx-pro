/* nx storage v2: superblock, mount rules and the KV store (src/nx/nx_store.c, nx_kv.c), including
 * a power cut at every flash operation of each write sequence (the W25Q16 model's power cuts). */
#include "harness.h"

#include <stdlib.h>

#include "nx_config.h"
#include "nx_crc.h"
#include "nx_flash.h"
#include "nx_kv.h"
#include "nx_store.h"
#include "nx_world.h"
#include "state.h"
#include "test_nx.h"

#define AREA_LO NX_SB_A
#define AREA_HI (NX_KV_START + NX_KV_SECTORS * NX_FLASH_SECTOR)

static uint8_t *snapshot;   /* 0x080000..AREA_HI */

static void remount(void)
{
    flash_power_on();
    nx_store_reset();
    nx_store_mount();
}

static void fresh(void)
{
    nxw_reset();
    g_ui_state = ST_HOME;
    flash_guard(AREA_LO, AREA_HI);   /* this test's writes stay in the superblock and KV sectors */
    nx_store_mount();
}

static int put_str(uint16_t key, const char *s)
{
    return nx_kv_put(key, s, (uint16_t)strlen(s));
}

/* The key reads back as one of `allowed` (NULL entry = absent), never as anything else. */
static int key_in(uint16_t key, const char *const *allowed, unsigned n)
{
    char buf[NX_KV_VAL_MAX + 1];
    int len = nx_kv_get(key, buf, NX_KV_VAL_MAX);
    for (unsigned i = 0; i < n; i++) {
        if (!allowed[i]) {
            if (len == NX_ENOTFOUND)
                return 1;
        } else if (len == (int)strlen(allowed[i]) && memcmp(buf, allowed[i], (size_t)len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int key_is(uint16_t key, const char *s)
{
    const char *one[1] = { s };
    return key_in(key, one, 1);
}

static void take_snapshot(void)
{
    if (!snapshot)
        snapshot = malloc(AREA_HI - AREA_LO);
    memcpy(snapshot, &flash_mem[AREA_LO], AREA_HI - AREA_LO);
}

static void restore_snapshot(void)
{
    memcpy(&flash_mem[AREA_LO], snapshot, AREA_HI - AREA_LO);
}

/* ---- the basics ---- */

static void test_basics(void)
{
    /* an erased region mounts EMPTY, without writing anything */
    fresh();
    CHECK_EQ(nx_store_state(), NX_STORE_EMPTY);
    CHECK_EQ(flash_op_count, 0);
    CHECK_EQ(nx_kv_get(1, 0, 0), NX_ENOTFOUND);
    CHECK(nx_store_super() == NULL);

    /* the first write formats (B, then A) and starts page 0 */
    CHECK_EQ(put_str(1, "hello"), NX_OK);
    CHECK_EQ(nx_store_state(), NX_STORE_OK);
    const struct nx_super *sb = nx_store_super();
    CHECK(sb && sb->magic == NX_SB_MAGIC && sb->layout_ver == NX_LAYOUT_VER && sb->kv_start == NX_KV_START);
    CHECK_EQ(sb->created_by_fw, NX_FW_ID);
    CHECK_EQ(sb->mcu_uid[0], 0xA0);
    CHECK(memcmp(&flash_mem[NX_SB_A], &flash_mem[NX_SB_B], sizeof *sb) == 0);
    CHECK_EQ(nx_kv_active_page(), 0);
    CHECK(key_is(1, "hello"));
    CHECK_EQ(flash_guard_violations, 0);

    /* after a remount: the same values; updates and deletes are appended */
    remount();
    CHECK_EQ(nx_store_state(), NX_STORE_OK);
    CHECK(key_is(1, "hello"));
    CHECK_EQ(put_str(2, "two"), NX_OK);
    CHECK_EQ(put_str(1, "hello again"), NX_OK);
    CHECK_EQ(nx_kv_count(), 2);
    remount();
    CHECK(key_is(1, "hello again"));
    CHECK(key_is(2, "two"));
    CHECK_EQ(nx_kv_del(2), NX_OK);
    CHECK_EQ(nx_kv_del(77), NX_OK);          /* a missing key: nothing written */
    remount();
    const char *absent[1] = { NULL };
    CHECK(key_in(2, absent, 1));
    CHECK_EQ(nx_kv_count(), 1);
    /* a short buffer gets the start of the value and the full length */
    char small[4];
    CHECK_EQ(nx_kv_get(1, small, sizeof small), 11);
    CHECK(memcmp(small, "hell", 4) == 0);
    CHECK_EQ(nx_kv_put(3, small, NX_KV_VAL_MAX + 1u), NX_ENOSPACE);

    /* compaction: a page full of updates moves the live values to the other page */
    uint8_t big[1000];
    for (unsigned i = 0; i < 40u; i++) {
        memset(big, (int)i, sizeof big);
        CHECK_EQ(nx_kv_put(5, big, sizeof big), NX_OK);
    }
    CHECK_EQ(nx_kv_active_page(), 0);         /* two compactions: 0 -> 1 -> 0 */
    CHECK_EQ(flash_erase_count(NX_KV_START), 1);                      /* erased for the second */
    CHECK_EQ(flash_erase_count(NX_KV_START + NX_KV_PAGE_BYTES), 0);   /* was still erased */
    remount();
    CHECK(key_is(1, "hello again"));
    uint8_t got[1000];
    CHECK_EQ(nx_kv_get(5, got, sizeof got), 1000);
    memset(big, 39, sizeof big);
    CHECK(memcmp(got, big, sizeof got) == 0);
    /* the live data must fit one page */
    unsigned stored = 0;
    int rc = NX_OK;
    for (uint16_t k = 100; k < 130 && rc == NX_OK; k++) {
        rc = nx_kv_put(k, big, sizeof big);
        stored += rc == NX_OK;
    }
    CHECK_EQ(rc, NX_ENOSPACE);
    CHECK(stored >= 12u && stored <= 15u);
    remount();
    CHECK(key_is(1, "hello again"));
    CHECK_EQ(flash_guard_violations, 0);

    /* no nx writes in PC mode */
    g_ui_state = ST_PC;
    unsigned ops = flash_op_count;
    CHECK_EQ(put_str(1, "pc"), NX_REFUSED_PC);
    CHECK_EQ(flash_op_count, ops);
}

static void test_foreign_data(void)
{
    /* data in the region without a superblock (a future vendor firmware?): read-only for good */
    fresh();
    flash_mem[NX_KV_START + 0x1234] = 0x42;
    remount();
    CHECK_EQ(nx_store_state(), NX_STORE_READONLY);
    CHECK_EQ(put_str(1, "x"), NX_EREADONLY);
    CHECK_EQ(flash_op_count, 0);
    fresh();
    memcpy(&flash_mem[NX_SB_A], "SPOOKY", 6);
    remount();
    CHECK_EQ(nx_store_state(), NX_STORE_READONLY);

    /* a layout that needs a newer firmware: read-only too */
    fresh();
    CHECK_EQ(put_str(1, "v1"), NX_OK);
    struct nx_super sb = *nx_store_super();
    sb.seq = 2;
    sb.min_reader_ver = NX_LAYOUT_VER + 1u;
    sb.crc32 = nx_crc32(0, &sb, (uint32_t)offsetof(struct nx_super, crc32));
    memset(&flash_mem[NX_SB_A], 0xFF, 0x1000);
    memcpy(&flash_mem[NX_SB_A], &sb, sizeof sb);
    remount();
    CHECK_EQ(nx_store_state(), NX_STORE_READONLY);
    CHECK_EQ(put_str(1, "v2"), NX_EREADONLY);

    /* our own format cut in the middle (a torn copy starts with the magic): formatted again */
    fresh();
    flash_cut_at(0, FLASH_CUT_TEAR_PAGE);
    CHECK_EQ(put_str(1, "a"), NX_EFLASH);
    remount();
    CHECK_EQ(nx_store_state(), NX_STORE_EMPTY);
    CHECK_EQ(put_str(1, "a"), NX_OK);
    remount();
    CHECK(key_is(1, "a"));
}

/* ---- power cuts: each sequence cut at every flash operation ---- */

typedef void (*sequence_fn)(void);

static void seq_first_writes(void)   /* from an erased region: format, page 0, three keys */
{
    put_str(1, "one");
    put_str(2, "two");
    put_str(1, "uno");
}

static void check_first_writes(void)
{
    const char *k1[3] = { NULL, "one", "uno" }, *k2[2] = { NULL, "two" };
    CHECK(nx_store_state() == NX_STORE_OK || nx_store_state() == NX_STORE_EMPTY);
    CHECK(key_in(1, k1, 3));
    CHECK(key_in(2, k2, 2));
}

static void setup_full_page(void)    /* keys 1..3 plus filler, page 0 nearly full */
{
    uint8_t v[900];
    memset(v, 0x33, sizeof v);
    put_str(1, "alpha");
    put_str(2, "beta");
    put_str(3, "gamma");
    for (unsigned i = 0; i < 16u && nx_kv_free() > 2u * 916u; i++)
        nx_kv_put(9, v, sizeof v);
}

static void seq_compaction(void)     /* the update that no longer fits: compaction to page 1 */
{
    uint8_t v[900];
    memset(v, 0x44, sizeof v);
    put_str(2, "beta-2");
    nx_kv_put(9, v, sizeof v);
    nx_kv_put(9, v, sizeof v);
    put_str(3, "gamma-2");
}

static void check_compaction(void)
{
    const char *k2[2] = { "beta", "beta-2" }, *k3[2] = { "gamma", "gamma-2" };
    CHECK_EQ(nx_store_state(), NX_STORE_OK);
    CHECK(key_is(1, "alpha"));
    CHECK(key_in(2, k2, 2));
    CHECK(key_in(3, k3, 2));
    uint8_t v[900];
    CHECK_EQ(nx_kv_get(9, v, sizeof v), 900);
    CHECK(v[0] == 0x33 || v[0] == 0x44);
    CHECK(v[899] == v[0]);
}

static void seq_settings_updates(void)   /* the everyday case: one small record, rewritten */
{
    char s[40];
    for (unsigned i = 0; i < 6u; i++) {
        snprintf(s, sizeof s, "settings-%u", i);
        put_str(0x0001, s);
    }
}

static void check_settings_updates(void)
{
    const char *allowed[8] = { NULL, "settings-0", "settings-1", "settings-2", "settings-3", "settings-4",
                               "settings-5", "base" };
    CHECK(key_in(0x0001, allowed, 8));
    CHECK(key_is(0x0002, "other"));
}

static void setup_settings(void)
{
    put_str(0x0001, "base");
    put_str(0x0002, "other");
}

static unsigned sweep(void (*setup)(void), sequence_fn seq, void (*check)(void))
{
    /* learn the number of flash operations, then cut before/inside each one in turn */
    fresh();
    if (setup)
        setup();
    take_snapshot();
    unsigned before = flash_op_count;
    seq();
    unsigned n = flash_op_count - before, cuts = 0;
    for (unsigned k = 0; k < n; k++) {
        for (unsigned mode = 0; mode < 2u; mode++) {
            fresh();
            restore_snapshot();
            remount();
            flash_cut_at(k, mode ? (FLASH_CUT_TEAR_PAGE | FLASH_CUT_TEAR_ERASE) : FLASH_CUT_CLEAN);
            seq();
            CHECK(flash_cut_happened());
            cuts++;
            remount();
            check();
            /* and the store keeps working afterwards */
            CHECK_EQ(put_str(0x0777, "after"), NX_OK);
            remount();
            CHECK(key_is(0x0777, "after"));
            check();
            CHECK_EQ(flash_guard_violations, 0);
        }
    }
    return cuts;
}

static void test_power_cuts(void)
{
    unsigned cuts = 0;
    cuts += sweep(0, seq_first_writes, check_first_writes);
    cuts += sweep(setup_full_page, seq_compaction, check_compaction);
    cuts += sweep(setup_settings, seq_settings_updates, check_settings_updates);
    printf("test_nx: storage power cuts: %u cut points\n", cuts);
    CHECK(cuts >= 60u);
}

static void test_wear(void)
{
    /* 3000 settings saves: compactions spread over both pages, few erases per sector */
    fresh();
    char s[48];
    for (unsigned i = 0; i < 3000u; i++) {
        snprintf(s, sizeof s, "settings record %u -----------------------", i);
        CHECK_EQ(put_str(0x0001, s), NX_OK);
    }
    unsigned max = 0;
    for (uint32_t a = NX_KV_START; a < AREA_HI; a += NX_FLASH_SECTOR)
        if (flash_erase_count(a) > max)
            max = flash_erase_count(a);
    CHECK(max >= 2u && max <= 8u);
    CHECK_EQ(flash_erase_count(NX_SB_A), 0);
    CHECK_EQ(flash_guard_violations, 0);
    remount();
    CHECK(key_is(0x0001, s));
}

void test_nx_store(void)
{
    test_basics();
    test_foreign_data();
    test_power_cuts();
    test_wear();
}
