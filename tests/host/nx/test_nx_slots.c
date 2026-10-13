/* Slot copy / move / erase on the device (src/nx/nx_slots.h) on the host board model: the bytes
 * and names a copy writes (the legacy layout, as a Spooky2 upload), move and erase, the refusals
 * (a used target, a loaded slot, PC mode, an empty source, a second request), no write in PC mode,
 * and the key path: program detail > LOOP > Copy > the program list > START. */
#include "harness.h"

#include "link_internal.h"
#include "nx_config.h"
#include "nx_core.h"
#include "nx_keys.h"
#include "nx_settings.h"
#include "nx_slots.h"
#include "nx_store.h"
#include "nx_ui.h"
#include "nx_ui_int.h"
#include "nx_uiset.h"
#include "nx_world.h"
#include "state.h"
#include "storage.h"
#include "test_nx.h"

#if NX_FEAT_SLOTS
#define SLOT_BYTES (PROG_PARAMS_OFFSET + PROG_PARAMS_BYTES)

static void store_program(unsigned slot, uint64_t f0, const char *name)
{
    struct program p;
    memset(&p, 0, sizeof p);
    p.hdr[HDR_WAVE] = 11;
    p.hdr[HDR_AMPL] = 1000;
    p.hdr[HDR_OFFS] = 120;
    p.hdr[HDR_DWELL] = 30;
    p.hdr[HDR_COUNT] = 3;
    for (unsigned i = 0; i < 3u; i++)
        p.freq[i] = f0 + 10u * i;
    uint16_t params[2 * PROGRAM_STEPS];
    for (unsigned i = 0; i < 2u * PROGRAM_STEPS; i++)
        params[i] = (uint16_t)(i * 7u);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot)], &p, PROG_RECORD_BYTES);
    memcpy(&flash_mem[PROG_SLOT_ADDR(slot) + PROG_PARAMS_OFFSET], params, PROG_PARAMS_BYTES);
    name_set(slot, name, (unsigned)strlen(name));
    names_save();
}

static int slot_erased(unsigned slot)
{
    for (uint32_t i = 0; i < 0x1000u; i++)
        if (flash_mem[PROG_SLOT_ADDR(slot) + i] != 0xFFu)
            return 0;
    return 1;
}

static int name_in_flash(unsigned slot, const char *name)
{
    return memcmp(&flash_mem[NAMES_ADDR + slot * NAME_LEN], name, strlen(name) + 1u) == 0;
}

static void press(uint8_t keys, uint32_t hold_ms)
{
    nxw_press(keys, hold_ms);
    nxw_run_ms(250);
}

void test_nx_slots(void)
{
    struct nx_uiset u;
    nx_uiset_defaults(&u);
    u.splash = NX_SPLASH_OFF;
    u.guide = 1;
    nx_uiset_to_blob(&u, g_nx_settings.ui);
    nxw_reset();
    nx_store_mount();
    store_program(4, 10000007u, "Lyme");
    store_program(5, 20000007u, "Detox");
    nxw_run_until(4600);

    /* copy 4 -> 9: the record, the gating pairs and the name, as Spooky2 would write them */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 4, 9), NX_OK);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 5, 10), NX_EPENDING);   /* one at a time */
    nxw_run_ms(100);
    CHECK(memcmp(&flash_mem[PROG_SLOT_ADDR(9)], &flash_mem[PROG_SLOT_ADDR(4)], SLOT_BYTES) == 0);
    CHECK(name_in_flash(9, "Lyme") && name_in_flash(4, "Lyme"));
    CHECK(strcmp(g_names[9], "Lyme") == 0);
    CHECK(nx_slot_used(9));

    /* refusals */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 4, 5), NX_ENOSPACE);   /* a program there */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 12, 13), NX_EINVAL);   /* nothing to copy */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 4, 4), NX_EINVAL);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 4, 31), NX_EINVAL);
    struct nx_action a = { NXA_LOAD, 1, 5, 0, 0 };
    CHECK_EQ(nx_core_act(&a), NX_OK);                                /* G2 has slot 5 */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_MOVE, 5, 12), NX_BUSY);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 5, 0), NX_BUSY);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 9, 5), NX_BUSY);       /* onto a loaded one */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_COPY, 5, 12), NX_OK);        /* reading a loaded one is fine */
    nxw_run_ms(100);
    CHECK(memcmp(&flash_mem[PROG_SLOT_ADDR(12)], &flash_mem[PROG_SLOT_ADDR(5)], SLOT_BYTES) == 0);

    /* move 9 -> 14: 9 erased and nameless */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_MOVE, 9, 14), NX_OK);
    nxw_run_ms(100);
    CHECK(memcmp(&flash_mem[PROG_SLOT_ADDR(14)], &flash_mem[PROG_SLOT_ADDR(4)], SLOT_BYTES) == 0);
    CHECK(slot_erased(9) && !nx_slot_used(9));
    CHECK(name_in_flash(14, "Lyme") && g_names[9][0] == 0 && flash_mem[NAMES_ADDR + 9u * NAME_LEN] == 0);

    /* erase 14 */
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 14, 0), NX_OK);
    nxw_run_ms(100);
    CHECK(slot_erased(14) && g_names[14][0] == 0);

    /* a request that finds no quiet moment (steps of 1 s) is dropped after NX_SLOTS_WAIT_MS, and
     * one waiting when PC mode starts is dropped too: the PC may rewrite the slots meanwhile */
    store_program(20, 10000007u, "Fast");
    struct program *fp = (struct program *)&flash_mem[PROG_SLOT_ADDR(20)];
    fp->hdr[HDR_DWELL] = 1;
    a = (struct nx_action){ NXA_LOAD, 0, 20, 0, 0 };
    CHECK_EQ(nx_core_act(&a), NX_OK);
    a.type = NXA_PLAY;
    a.v = 1;
    CHECK_EQ(nx_core_act(&a), NX_OK);
    nxw_run_ms(500);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 12, 0), NX_OK);
    nxw_run_ms(NX_SLOTS_WAIT_MS - 1000u);
    CHECK(!slot_erased(12));
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 4, 0), NX_EPENDING);
    nxw_run_ms(2000);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 12, 0), NX_OK);   /* the first one was dropped */
    nxw_pb1 = 1;
    nxw_run_ms(1000);
    nxw_pb1 = 0;
    nxw_run_ms(3000);   /* PC mode unloaded the generators: quiet now, but the request is gone */
    CHECK(!slot_erased(12));
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 12, 0), NX_OK);
    nxw_run_ms(100);
    CHECK(slot_erased(12));
    store_program(12, 20000007u, "Detox");

    /* PC mode: refused */
    nxw_pb1 = 1;
    nxw_run_ms(1000);
    CHECK_EQ(nx_slots_request(NX_SLOTOP_ERASE, 12, 0), NX_REFUSED_PC);
    nxw_pb1 = 0;
    nxw_run_ms(1000);
    CHECK(!slot_erased(12));   /* (stored again above) */

    /* the keys: detail of slot 4 (LOOP held in the list) > LOOP > Copy > DOWN to 7 > START */
    press(NX_KEY_DOWN, 100);   /* the menu, on Settings */
    nx_ui_reset(&nx_scr_dash, 0, NX_TR_NONE);
    nx_ui_push(&nx_scr_detail, 4, NX_TR_NONE);
    nxw_run_ms(100);
    press(NX_KEY_LOOP, 100);
    CHECK(nx_ui_top() == &nx_scr_slotact);
    press(NX_KEY_DOWN, 100);   /* Copy */
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_lib);
    press(NX_KEY_START, 100);  /* the first empty slot after 4: 6 */
    CHECK(nx_ui_top() == &nx_scr_detail);
    nxw_run_ms(500);
    CHECK(memcmp(&flash_mem[PROG_SLOT_ADDR(6)], &flash_mem[PROG_SLOT_ADDR(4)], SLOT_BYTES) == 0);
    CHECK(strcmp(g_names[6], "Lyme") == 0);

    /* ... erase 6: LOOP > DOWN x3 (Erase) > START > hold START */
    nx_ui_push(&nx_scr_detail, 6, NX_TR_NONE);
    nxw_run_ms(100);
    press(NX_KEY_LOOP, 100);
    for (unsigned i = 0; i < 3u; i++)
        press(NX_KEY_DOWN, 100);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_confirm);
    press(NX_KEY_START, NX_CONFIRM_MS + 200u);
    nxw_run_ms(500);
    CHECK(slot_erased(6));
    CHECK(nx_ui_top() == &nx_scr_detail);   /* slot 4's again */

    /* play on both */
    press(NX_KEY_LOOP, 100);
    press(NX_KEY_START, 100);
    CHECK(nx_ui_top() == &nx_scr_dash);
    CHECK(g_ch[0].prog_slot == 4 && g_ch[1].prog_slot == 4);
    CHECK((g_ui_flags & UIF_RUN_G1) && (g_ui_flags & UIF_RUN_G2));
    memset(g_nx_settings.ui, 0, sizeof g_nx_settings.ui);
}
#else
void test_nx_slots(void)
{
}
#endif
