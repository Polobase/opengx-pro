/* Fault handling, reset reason and boot-loop guard (docs/nx/design/core.md §5).
 *
 * The crash record lives in the .noinit section, which ld/nx.ld places at NX_NOINIT_ADDR (a RAM
 * area the vendor bootloader leaves alone, see docs/nx/drop0-checklist.md §1) and reset_c never
 * clears. It is valid when its magic and CRC match; after a power-on its RAM is random, so it is
 * created afresh.
 *
 * HardFault and unexpected interrupts (startup.c, CFG_FAULT_HOOK=nx_fault) record the stacked
 * PC/LR/xPSR, the exception number and the uptime, then reset the MCU; the next boot sees a
 * software reset with a pending fault. A watchdog reset or a fault reset is "abnormal". When
 * NX_BOOTLOOP_N abnormal resets follow each other, each before NX_BOOTLOOP_WINDOW_MS of uptime,
 * the fault handler sends the power-off signal instead of resetting, and a boot after such a
 * streak sets safe mode (g_nx_diag.safe_mode). */
#pragma once

#include <stdint.h>

#define NX_NOINIT_MAGIC 0x494E584Eu   /* "NXNI" */

enum nx_reset {
    NX_RST_UNKNOWN = 0,
    NX_RST_POWER,      /* power-on / brown-out (PWRRSTF) */
    NX_RST_PIN,        /* NRST pin only */
    NX_RST_SOFTWARE,   /* SYSRESETREQ without a recorded fault (e.g. w95) */
    NX_RST_WATCHDOG,   /* IWDG or WWDG */
    NX_RST_FAULT,      /* SYSRESETREQ from the fault handler */
    NX_RST_LOWPOWER,
    NX_RST_OPTION,     /* option-byte loading */
};

struct nx_noinit {
    uint32_t magic;          /* NX_NOINIT_MAGIC */
    uint32_t boots;          /* boots since the record was created */
    uint32_t crash_count;    /* faults since the record was created */
    uint32_t pc, lr, xpsr;   /* stacked registers of the last fault (0 if the stack was invalid) */
    uint32_t ipsr;           /* exception number of the last fault (3 = HardFault) */
    uint32_t uptime_ms;      /* uptime at the last fault */
    uint8_t fault_pending;   /* the fault handler reset the MCU; cleared by the next boot */
    uint8_t streak;          /* abnormal resets in a row */
    uint8_t alive;           /* this boot has run NX_BOOTLOOP_WINDOW_MS */
    uint8_t rsv;
    uint32_t crc;            /* fmt_crc32 of the bytes before it */
};

extern struct nx_noinit g_nx_noinit;

/* Pure record logic (host-tested): */
int nx_noinit_valid(const struct nx_noinit *r);
void nx_noinit_seal(struct nx_noinit *r);   /* recompute the CRC */
/* Boot: create the record if invalid, classify the reset from the RCC_CSR value, update the
 * streak, count the boot. Returns the reason. */
enum nx_reset nx_fault_boot(struct nx_noinit *r, uint32_t csr);
int nx_fault_safe_mode(const struct nx_noinit *r);
/* Fault: record it (frame = the stacked exception frame or 0). Returns 1 = reset, 0 = power off. */
int nx_fault_record(struct nx_noinit *r, const uint32_t *frame, uint32_t ipsr, uint32_t now_ms);
/* Main loop: marks the boot as alive once NX_BOOTLOOP_WINDOW_MS of uptime have passed. */
void nx_fault_task(uint32_t now_ms);

/* Device: */
void nx_fault_init(void);   /* first thing in main: reads RCC_CSR, clears it (RMVF), nx_fault_boot, diag */
void nx_fault(const uint32_t *frame, uint32_t ipsr);   /* CFG_FAULT_HOOK; never returns */
