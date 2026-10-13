/* Resume after a power cut (docs/nx/fw2-design.md, "Resume"): what each generator plays, kept in
 * the KV store and offered again at the next boot.
 *
 *   - nx_resume_task writes the snapshot when a generator's slot, run or loop flag changes, and
 *     every NX_RESUME_EVERY_MS while one runs (so at most that much of a program repeats). Never
 *     in PC or calibration mode or before the boot gate, and only when both queues are idle and
 *     no running step ends within NX_RESUME_QUIET_MS (a put takes about 1 ms; one that has to
 *     compact the page waits for a whole quiet second, as the settings save does).
 *   - A generator is kept only while it plays or is paused in a stored slot whose program is the
 *     one in RAM (CRC-32 of the record): after a PC upload into the channel it is not. A built-in
 *     Terrain set (nx_src.h) is kept with its protocol position; it resumes while the image has
 *     that set.
 *   - The snapshot is deleted when nothing is left to resume and at a clean power-off, so only a
 *     power cut (or a crash) leads to an offer.
 *   - nx_resume_boot turns a stored snapshot into the offer, unless the setting is off or the
 *     boot-loop guard tripped; each slot must still hold the same program. The UI shows it after
 *     the boot gate and answers with nx_resume_accept (START, or its countdown) or
 *     nx_resume_discard (STOP). A PC session cancels it. */
#pragma once

#include <stdint.h>

#define NX_KEY_RESUME 0x0002u
#define NX_RESUME_VER 1u
#define NX_RESUME_EVERY_MS 60000u
#define NX_RESUME_QUIET_MS 100u
#define NX_RESUME_MIN_GAP_MS 2000u   /* between two writes: pausing and resuming in a row */

enum { NX_RES_RUN = 1u, NX_RES_LOOP = 2u };

struct nx_resume_gen {
    uint8_t slot;          /* 1..30 or a Terrain set (nx_src.h), 0 = nothing to resume */
    uint8_t flags;         /* NX_RES_RUN, NX_RES_LOOP (not running: paused) */
    uint16_t chain;        /* its chain position (nx_chain.h): id | item << 3 | plan << 7 | pass << 9; 0 = none;
                            * a set: its Terrain run (nx_terrain.h): id | item << 3 | power << 8 */
    uint32_t elapsed_ms;   /* g_ch[].elapsed */
    uint32_t crc;          /* nx_crc32 of the slot's program record; a set: the Terrain item's run_s */
};

struct nx_resume_rec {
    uint8_t ver;           /* NX_RESUME_VER */
    uint8_t rsv[3];
    struct nx_resume_gen g[2];
};

/* Auto-start (g_nx_settings.autostart, NX_FEAT_TIMERS): the slot and loop flag each generator
 * last started are kept under NX_KEY_LAST (written, like the snapshot, only when quiet and only
 * while the setting is on). At a power-on or reset-pin boot with PB1 low, no key held (POWER
 * aside: it switched the unit on), no safe mode and nothing to resume, they become the offer
 * instead (nx_resume_auto() = 1); accepting loads them and plays them from the start. */
#define NX_KEY_LAST 0x0003u
struct nx_last_rec {
    uint8_t ver;       /* NX_RESUME_VER */
    uint8_t slot[2];   /* 1..30, 0 = none */
    uint8_t loop;      /* bit ch */
};

void nx_resume_boot(void);          /* after nx_settings_load, before the loop */
unsigned nx_resume_offer(void);     /* bit ch: the offer resumes that generator; 0 = no offer */
int nx_resume_auto(void);           /* 1: the offer is the auto-start */
int nx_resume_accept(void);         /* loads, seeks, plays or pauses; NX_OK or the core's refusal */
void nx_resume_discard(void);
void nx_resume_task(uint32_t now);  /* main loop */
void nx_resume_power_off(void);     /* clean power-off: nothing to resume */
/* What the generators play now, without the CRCs (0): the part the change check compares. */
void nx_resume_capture(struct nx_resume_rec *r);
