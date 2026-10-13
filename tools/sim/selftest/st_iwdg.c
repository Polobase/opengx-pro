/* Independent watchdog (tools/sim/selftest.py "iwdg"): write protection, LSI forced on, PVU/RVU
 * after writes, five reloads 5 ms apart, then no more: the reset comes 10 ms after the last
 * reload. The boot after it records RCC_CSR. Results in KEEP (outside .bss). */
#include "st.h"

#define RCC_CSR   0x40021060u
#define IWDG_KR   0x40003000u
#define IWDG_PR   0x40003004u
#define IWDG_RLR  0x40003008u
#define IWDG_SR   0x4000300Cu
#define SYST_CSR  0xE000E010u
#define SYST_RVR  0xE000E014u
#define SYST_CVR  0xE000E018u
#define KEEP      ((volatile uint32_t *)0x20001000u)

enum { K_SR_WAIT, K_PR_LOCKED, K_LSIRDY, K_FED, K_CSR2 };

static volatile uint32_t ms;

void SysTick_Handler(void)
{
    ms++;
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [15] = SysTick_Handler,
};

int main(void)
{
    uint32_t csr = REG(RCC_CSR);
    if (csr & (1u << 29)) {                     /* IWDGRSTF: the boot after the watchdog reset */
        KEEP[K_CSR2] = csr;
        res[15] = DONE;
        return 0;
    }
    REG(RCC_CSR) |= 1u << 23;                   /* RMVF: forget the power-on flags */
    REG(SYST_RVR) = 63999u;                     /* 1 kHz */
    REG(SYST_CVR) = 0;
    REG(SYST_CSR) = 7u;
    REG(IWDG_KR) = 0xCCCCu;                     /* start: 4096 counts of LSI/4 = 512 ms */
    REG(IWDG_PR) = 3u;                          /* write-protected: ignored */
    KEEP[K_PR_LOCKED] = REG(IWDG_PR);
    KEEP[K_LSIRDY] = REG(RCC_CSR) >> 1 & 1u;    /* the watchdog forces the LSI on */
    REG(IWDG_KR) = 0x5555u;
    REG(IWDG_PR) = 0u;                          /* LSI / 4 = 8 kHz */
    REG(IWDG_RLR) = 79u;                        /* 80 counts = 10 ms */
    uint32_t n = 0;
    while (REG(IWDG_SR) && n < 100000u)
        n++;
    KEEP[K_SR_WAIT] = n;
    REG(IWDG_KR) = 0xAAAAu;                     /* reload with the new values */
    for (uint32_t fed = 1; fed <= 5u; fed++) {
        uint32_t t = ms;
        while (ms - t < 5u)
            __asm volatile("wfi");
        REG(IWDG_KR) = 0xAAAAu;
        KEEP[K_FED] = fed;
    }
    for (;;)
        __asm volatile("wfi");                  /* no more reloads */
}
