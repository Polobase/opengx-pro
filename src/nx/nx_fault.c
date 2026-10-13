/* Fault handling, reset reason and boot-loop guard (nx_fault.h). */
#include "nx_fault.h"

#include <stddef.h>
#include <string.h>

#include "fmt.h"
#include "nx_config.h"
#include "nx_diag.h"

/* RCC_CSR reset flags (RM0454), spelled out so the pure logic builds on the host too. */
#define CSR_OBLRSTF (1u << 25)
#define CSR_PINRSTF (1u << 26)
#define CSR_PWRRSTF (1u << 27)
#define CSR_SFTRSTF (1u << 28)
#define CSR_IWDGRSTF (1u << 29)
#define CSR_WWDGRSTF (1u << 30)
#define CSR_LPWRRSTF (1u << 31)

#ifdef HOST_TEST
#define NOINIT
#else
#define NOINIT __attribute__((section(".noinit")))
#endif

NOINIT struct nx_noinit g_nx_noinit;

static uint32_t record_crc(const struct nx_noinit *r)
{
    return fmt_crc32(0, (const uint8_t *)r, (uint32_t)offsetof(struct nx_noinit, crc));
}

int nx_noinit_valid(const struct nx_noinit *r)
{
    return r->magic == NX_NOINIT_MAGIC && r->crc == record_crc(r);
}

void nx_noinit_seal(struct nx_noinit *r)
{
    r->crc = record_crc(r);
}

static void fresh(struct nx_noinit *r)
{
    memset(r, 0, sizeof *r);
    r->magic = NX_NOINIT_MAGIC;
}

static enum nx_reset classify(uint32_t csr, int fault_pending)
{
    if (csr & (CSR_IWDGRSTF | CSR_WWDGRSTF))
        return NX_RST_WATCHDOG;
    if (csr & CSR_LPWRRSTF)
        return NX_RST_LOWPOWER;
    /* The flags are cleared at every boot (RMVF), so PWRRSTF means a power-on since then. On the
     * unit a power-on reads SFT PWR PIN: the vendor bootloader adds a software reset
     * (docs/nx/drop0-checklist.md §1, 2026-09-25), so the power-on flag wins over it. */
    if (csr & CSR_PWRRSTF)
        return NX_RST_POWER;
    if (csr & CSR_SFTRSTF)
        return fault_pending ? NX_RST_FAULT : NX_RST_SOFTWARE;
    if (csr & CSR_OBLRSTF)
        return NX_RST_OPTION;
    if (csr & CSR_PINRSTF)   /* every internal reset also pulls NRST: only when nothing else is set */
        return NX_RST_PIN;
    return NX_RST_UNKNOWN;
}

enum nx_reset nx_fault_boot(struct nx_noinit *r, uint32_t csr)
{
    if (!nx_noinit_valid(r))
        fresh(r);
    enum nx_reset why = classify(csr, r->fault_pending);
    if (why == NX_RST_WATCHDOG || why == NX_RST_FAULT)
        r->streak = r->alive ? 1u : (uint8_t)(r->streak < 255u ? r->streak + 1u : 255u);
    else
        r->streak = 0;
    r->fault_pending = 0;
    r->alive = 0;
    r->boots++;
    nx_noinit_seal(r);
    return why;
}

int nx_fault_safe_mode(const struct nx_noinit *r)
{
    return r->streak >= NX_BOOTLOOP_N;
}

int nx_fault_record(struct nx_noinit *r, const uint32_t *frame, uint32_t ipsr, uint32_t now_ms)
{
    if (!nx_noinit_valid(r))
        fresh(r);
    r->crash_count++;
    r->lr = frame ? frame[5] : 0;   /* exception frame: r0 r1 r2 r3 r12 lr pc xpsr */
    r->pc = frame ? frame[6] : 0;
    r->xpsr = frame ? frame[7] : 0;
    r->ipsr = ipsr;
    r->uptime_ms = now_ms;
    r->fault_pending = 1;
    unsigned next = (r->alive || now_ms >= NX_BOOTLOOP_WINDOW_MS) ? 1u : r->streak + 1u;
    nx_noinit_seal(r);
    return next < NX_BOOTLOOP_N;
}

void nx_fault_task(uint32_t now_ms)
{
    if (!g_nx_noinit.alive && now_ms >= NX_BOOTLOOP_WINDOW_MS && nx_noinit_valid(&g_nx_noinit)) {
        g_nx_noinit.alive = 1;
        nx_noinit_seal(&g_nx_noinit);
    }
}

#ifndef HOST_TEST
#include "board.h"
#include "nx_time.h"

void nx_fault_init(void)
{
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;   /* the next boot sees only its own reset cause */
    enum nx_reset why = nx_fault_boot(&g_nx_noinit, csr);
    g_nx_diag.reset_flags = csr;
    g_nx_diag.reset_reason = (uint8_t)why;
    g_nx_diag.safe_mode = (uint8_t)nx_fault_safe_mode(&g_nx_noinit);
}

void nx_fault(const uint32_t *frame, uint32_t ipsr)
{
    __disable_irq();
    uint32_t a = (uint32_t)frame;
    if (a < 0x20000000u || a > 0x20009000u - 32u || (a & 3u))
        frame = 0;   /* a corrupt stack pointer: record no registers rather than fault again */
    if (nx_fault_record(&g_nx_noinit, frame, ipsr, g_nx_ms))
        NVIC_SystemReset();
    power_off_signal();   /* boot loop: stay off (board.c feeds the watchdog, CFG_IWDG) */
}
#endif
