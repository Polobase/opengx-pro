/* Crash record, reset reason and boot-loop guard (src/nx/nx_fault.c, pure part). */
#include "harness.h"

#include "nx_config.h"
#include "nx_fault.h"
#include "test_nx.h"

#define PIN (1u << 26)
#define PWR (1u << 27)
#define SFT (1u << 28)
#define IWDG (1u << 29)
#define LPWR (1u << 31)

void test_nx_fault(void)
{
    struct nx_noinit r;

    /* power-on: random RAM is not a record; a fresh one is made */
    memset(&r, 0x5A, sizeof r);
    CHECK(!nx_noinit_valid(&r));
    CHECK_EQ(nx_fault_boot(&r, PWR | PIN), NX_RST_POWER);
    CHECK(nx_noinit_valid(&r));
    CHECK_EQ(r.boots, 1);
    CHECK_EQ(r.crash_count, 0);
    CHECK_EQ(r.streak, 0);

    /* reset causes; every internal reset also sets PINRSTF */
    CHECK_EQ(nx_fault_boot(&r, PIN), NX_RST_PIN);
    CHECK_EQ(nx_fault_boot(&r, SFT | PWR | PIN), NX_RST_POWER);   /* a power-on on the unit: the bootloader adds SFT */
    CHECK_EQ(nx_fault_boot(&r, SFT | PIN), NX_RST_SOFTWARE);   /* w95 */
    CHECK_EQ(r.streak, 0);
    CHECK_EQ(nx_fault_boot(&r, IWDG | PIN), NX_RST_WATCHDOG);
    CHECK_EQ(r.streak, 1);
    CHECK_EQ(nx_fault_boot(&r, LPWR | PIN), NX_RST_LOWPOWER);
    CHECK_EQ(nx_fault_boot(&r, 0), NX_RST_UNKNOWN);
    CHECK_EQ(r.boots, 7);

    /* a fault: registers from the stacked frame, pending flag, then a reset */
    static const uint32_t frame[8] = { 0, 1, 2, 3, 12, 0x08001235u, 0x08004567u, 0x21000000u };
    nx_fault_boot(&r, PWR | PIN);
    CHECK_EQ(nx_fault_record(&r, frame, 3, 1234), 1);
    CHECK(nx_noinit_valid(&r));
    CHECK_EQ(r.crash_count, 1);
    CHECK_EQ(r.pc, 0x08004567u);
    CHECK_EQ(r.lr, 0x08001235u);
    CHECK_EQ(r.xpsr, 0x21000000u);
    CHECK_EQ(r.ipsr, 3);
    CHECK_EQ(r.uptime_ms, 1234);
    CHECK_EQ(nx_fault_boot(&r, SFT | PIN), NX_RST_FAULT);
    CHECK_EQ(r.fault_pending, 0);
    CHECK_EQ(r.streak, 1);
    CHECK_EQ(nx_fault_safe_mode(&r), 0);

    /* the second fault within 30 s still resets; the third in a row powers off */
    CHECK_EQ(nx_fault_record(&r, 0, 16, 500), 1);
    CHECK_EQ(r.pc, 0);   /* no valid frame: no registers */
    CHECK_EQ(nx_fault_boot(&r, SFT | PIN), NX_RST_FAULT);
    CHECK_EQ(r.streak, 2);
    CHECK_EQ(nx_fault_record(&r, frame, 3, 500), 0);
    /* a watchdog loop: the third abnormal reset in a row sets safe mode */
    nx_fault_boot(&r, PWR | PIN);
    nx_fault_boot(&r, IWDG | PIN);
    nx_fault_boot(&r, IWDG | PIN);
    CHECK_EQ(nx_fault_safe_mode(&r), 0);
    nx_fault_boot(&r, IWDG | PIN);
    CHECK_EQ(r.streak, 3);
    CHECK_EQ(nx_fault_safe_mode(&r), 1);
    /* a boot that ran 30 s ends the streak: the next abnormal reset starts at 1 */
    g_nx_noinit = r;
    nx_fault_task(NX_BOOTLOOP_WINDOW_MS - 1u);
    CHECK_EQ(g_nx_noinit.alive, 0);
    nx_fault_task(NX_BOOTLOOP_WINDOW_MS);
    CHECK_EQ(g_nx_noinit.alive, 1);
    CHECK(nx_noinit_valid(&g_nx_noinit));
    CHECK_EQ(nx_fault_record(&g_nx_noinit, frame, 3, 40000), 1);
    CHECK_EQ(nx_fault_boot(&g_nx_noinit, SFT | PIN), NX_RST_FAULT);
    CHECK_EQ(g_nx_noinit.streak, 1);
    /* a fault after 30 s of uptime counts as the first even if the loop never marked it */
    r = g_nx_noinit;
    r.streak = 2;
    nx_noinit_seal(&r);
    CHECK_EQ(nx_fault_record(&r, frame, 3, NX_BOOTLOOP_WINDOW_MS + 5u), 1);
    /* a normal reset ends the streak */
    CHECK_EQ(nx_fault_boot(&r, PIN), NX_RST_PIN);
    CHECK_EQ(r.streak, 0);
    /* a corrupted record is replaced, not trusted */
    r.crash_count = 99;
    CHECK(!nx_noinit_valid(&r));
    nx_fault_boot(&r, PIN);
    CHECK_EQ(r.crash_count, 0);
}
