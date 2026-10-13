/* Slot copy / move / erase on the device (menu of the program detail, NX_FEAT_SLOTS).
 *
 * They write the legacy program and name areas exactly as a Spooky2 upload does: the target's
 * program sector erased and written (record and gating pairs, in 256-byte pieces read back and
 * compared), the name table rewritten (names_save). A request is checked at once and runs later
 * from the main loop, when no running step ends within a second (a sector erase can take 400 ms);
 * never in PC mode, and never on a slot a generator has loaded. The UI gets NXE_SLOT_DONE
 * (ch = 0 done, 1 failed; a = op << 8 | the slot to show). */
#pragma once

#include <stdint.h>

enum { NX_SLOTOP_COPY = 1, NX_SLOTOP_MOVE, NX_SLOTOP_ERASE };

/* NX_OK (queued), NX_REFUSED_PC, NX_BUSY (a generator has one of the slots loaded), NX_EPENDING
 * (another request waits), NX_ENOSPACE (the target holds a program: erase it first), NX_EINVAL.
 * A waiting request is dropped when PC mode starts (the PC may rewrite the slots) and when no
 * quiet moment comes within NX_SLOTS_WAIT_MS (a program with very short steps runs): the UI then
 * gets NXE_SLOT_DONE with ch = 2. */
#define NX_SLOTS_WAIT_MS 10000u
int nx_slots_request(uint8_t op, unsigned from, unsigned to);
void nx_slots_task(void);   /* main loop */
void nx_slots_reset(void);
int nx_slot_used(unsigned slot);   /* 1: slot 1..30 holds a program (count 1..200, dwell > 0) */
