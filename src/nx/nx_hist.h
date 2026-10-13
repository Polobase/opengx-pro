/* History and usage totals (menu > History, NX_FEAT_HIST).
 *
 * Every program run of at least NX_HIST_MIN_S seconds becomes a 64-byte record in a ring in the
 * reserved history region (NX_HIST_START, NX_HIST_SECTORS sectors of 64 records: 2048, the
 * oldest sector erased when the ring comes round). A run starts when a generator starts from
 * stopped and ends when it finishes, is stopped (STOP, a new program, its timer), enters PC mode,
 * or loses power: the resume snapshot found at the next boot becomes a "power cut" record. Paused
 * time does not count; a looped program is one run.
 *
 * Records wait in RAM (NX_HIST_PEND) and are written like the settings: never in PC mode and only
 * when no running step ends soon (a whole quiet second before a sector erase). Each record has
 * its sequence number and a CRC, so a write cut by a power loss is skipped. The totals (all-time
 * running time, runs, runs per slot, sessions) live in the KV store under NX_KEY_TOTALS.
 *
 * There is no wall clock: a record carries its session (a boot in which something was played)
 * and the uptime at its start. */
#pragma once

#include <stdint.h>

#include "nx_store.h"

#define NX_KEY_TOTALS 0x0005u
#define NX_HIST_REC_BYTES 64u
#define NX_HIST_PER_SECTOR (0x1000u / NX_HIST_REC_BYTES)
#define NX_HIST_N (NX_HIST_SECTORS * NX_HIST_PER_SECTOR)
#define NX_HIST_MIN_S 5u
#define NX_HIST_PEND 4u
#define NX_HIST_NAME 40u

enum nx_hist_end { NX_HEND_FINISHED = 1, NX_HEND_STOPPED, NX_HEND_TIMER, NX_HEND_CUT, NX_HEND_PC };

struct nx_hist_rec {
    uint32_t seq;               /* 1, 2, ... (0xFFFFFFFF: erased) */
    uint16_t session;           /* the totals' session number */
    uint8_t ch, slot;
    uint32_t start_s;           /* uptime at the start of the run */
    uint32_t run_s;             /* seconds it ran */
    uint8_t end;                /* enum nx_hist_end */
    uint8_t rsv[3];
    char name[NX_HIST_NAME];    /* the slot's name at the time (NUL-padded) */
    uint32_t crc;               /* nx_crc32 of the bytes before it */
};

struct nx_totals {
    uint8_t ver;
    uint8_t rsv;
    uint16_t sessions;
    uint32_t runs;
    uint32_t run_s;             /* all-time running time */
    uint16_t slot_runs[30];
};

void nx_hist_boot(void);        /* after nx_store_mount, before nx_resume_boot: the ring's head, the totals */
void nx_hist_task(uint32_t now);   /* main loop: notices runs and their ends, writes when quiet */
/* ch's run ends now for this reason (runner: finished; timer): recorded at once, so a chain's
 * next program, even of the same slot, is a run of its own. */
void nx_hist_end(unsigned ch, uint8_t end);
/* Power-off: the runs end ("stopped") and the waiting records are written (not in PC mode). */
void nx_hist_power_off(void);
/* A run to record (the resume boot: a power cut; the task: an ended run). run_s below
 * NX_HIST_MIN_S is dropped. */
void nx_hist_add(unsigned ch, unsigned slot, uint32_t start_s, uint32_t run_s, uint8_t end);
unsigned nx_hist_count(void);   /* records that can be read */
int nx_hist_get(unsigned i, struct nx_hist_rec *r);   /* i = 0: the newest; 1 = valid */
const struct nx_totals *nx_hist_totals(void);
int nx_hist_pending(void);      /* records not written yet */
