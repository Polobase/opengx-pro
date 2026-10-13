/* Slot copy / move / erase on the device (nx_slots.h). */
#include "nx_slots.h"

#include <string.h>

#include "link_internal.h"
#include "nx_config.h"
#include "nx_runner.h"
#include "nx_time.h"
#include "nx_ui.h"
#include "nx_vm.h"
#include "spiflash.h"
#include "state.h"
#include "storage.h"

#define SLOT_BYTES (PROG_PARAMS_OFFSET + PROG_PARAMS_BYTES)   /* record + gating pairs */
#define CHUNK 256u

int nx_slot_used(unsigned slot)
{
    if (slot < 1u || slot > PROGRAM_SLOTS)
        return 0;
    uint64_t hdr[5];
    storage_slot_read(slot, 0, hdr, sizeof hdr);
    return hdr[HDR_COUNT] >= 1u && hdr[HDR_COUNT] <= PROGRAM_STEPS && hdr[HDR_DWELL] != 0u;   /* as program_load */
}

#if NX_FEAT_SLOTS
static uint8_t s_op, s_from, s_to;   /* the waiting request (s_op 0: none) */
static uint32_t s_t0;                /* its time */

void nx_slots_reset(void)
{
    s_op = 0;
}

static int loaded(unsigned slot)
{
    return g_ch[0].prog_slot == slot || g_ch[1].prog_slot == slot;
}

static int check(uint8_t op, unsigned from, unsigned to)
{
    if (g_ui_state == ST_PC)
        return NX_REFUSED_PC;
    if (op < NX_SLOTOP_COPY || op > NX_SLOTOP_ERASE || from < 1u || from > PROGRAM_SLOTS)
        return NX_EINVAL;
    if (op == NX_SLOTOP_ERASE)
        return loaded(from) ? NX_BUSY : NX_OK;
    if (to < 1u || to > PROGRAM_SLOTS || to == from || !nx_slot_used(from))
        return NX_EINVAL;
    if (loaded(to) || (op == NX_SLOTOP_MOVE && loaded(from)))
        return NX_BUSY;
    return nx_slot_used(to) ? NX_ENOSPACE : NX_OK;
}

int nx_slots_request(uint8_t op, unsigned from, unsigned to)
{
    if (s_op)
        return NX_EPENDING;
    int rc = check(op, from, to);
    if (rc == NX_OK) {
        s_op = op;
        s_from = (uint8_t)from;
        s_to = (uint8_t)to;
        s_t0 = nx_ms();
    }
    return rc;
}

static int copy(unsigned from, unsigned to)
{
    uint8_t buf[CHUNK], back[CHUNK];
    if (spiflash_sector_erase(PROG_SLOT_ADDR(to)))
        return NX_EFLASH;
    for (uint32_t off = 0; off < SLOT_BYTES; off += CHUNK) {
        uint32_t n = SLOT_BYTES - off < CHUNK ? SLOT_BYTES - off : CHUNK;
        storage_slot_read(from, off, buf, n);
        spiflash_write(PROG_SLOT_ADDR(to) + off, buf, n);
        storage_slot_read(to, off, back, n);
        if (memcmp(buf, back, n))
            return NX_EFLASH;
    }
    memcpy(g_names[to], g_names[from], NAME_LEN);
    return NX_OK;
}

void nx_slots_task(void)
{
    if (!s_op)
        return;
    if (g_ui_state == ST_PC) {   /* the PC may rewrite the slots: the request goes */
        s_op = 0;
        return;
    }
    /* two sector erases (copy, erase) or three (move) of up to 400 ms each */
    if (!nx_runner_quiet(s_op == NX_SLOTOP_MOVE ? 1500u : 1000u)) {
        if (nx_ms() - s_t0 >= NX_SLOTS_WAIT_MS) {
            nx_ui_post(NXE_SLOT_DONE, 2, (uint16_t)(s_op << 8 | (s_op == NX_SLOTOP_ERASE ? s_from : s_to)));
            s_op = 0;
        }
        return;
    }
    uint8_t op = s_op;
    s_op = 0;
    int rc = check(op, s_from, s_to);   /* the generators may have loaded a slot meanwhile */
    if (rc == NX_OK && op != NX_SLOTOP_ERASE)
        rc = copy(s_from, s_to);
    if (rc == NX_OK && op != NX_SLOTOP_COPY) {
        program_erase(s_from);
        memset(g_names[s_from], 0, NAME_LEN);
    }
    if (rc == NX_OK)
        names_save();
    nx_ui_post(NXE_SLOT_DONE, rc != NX_OK, (uint16_t)(op << 8 | (op == NX_SLOTOP_ERASE ? s_from : s_to)));
}
#else
void nx_slots_reset(void)
{
}

int nx_slots_request(uint8_t op, unsigned from, unsigned to)
{
    (void)op;
    (void)from;
    (void)to;
    return NX_EINVAL;
}

void nx_slots_task(void)
{
}
#endif
