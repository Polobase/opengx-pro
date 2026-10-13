/* Emulator self-test (tools/sim/selftest.py): interrupt priorities and preemption, PRIMASK,
 * PendSV, SysTick, WFI, TIM3 rate, USART receive interrupt. Results go to res[]. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

volatile uint32_t res[8];
volatile uint32_t flag_u1, tim_count, systick_count, tim_spin_ok, pendsv_ran, rx_byte;
extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);

void TIM3_IRQHandler(void)            /* priority 1 */
{
    REG(0x40000410) = ~1u;              /* TIM3_SR: clear UIF */
    tim_count++;
    if (tim_count == 5) {               /* only a preempting USART1 interrupt can end this wait */
        uint32_t n = 0;
        while (!flag_u1 && n < 2000000u)
            n++;
        tim_spin_ok = flag_u1 ? 1u : 2u;
    }
}

void USART1_IRQHandler(void)          /* priority 0 */
{
    rx_byte = REG(0x40013824) & 0xFF;   /* RDR */
    flag_u1++;
}

void SysTick_Handler(void) { systick_count++; }
void PendSV_Handler(void) { pendsv_ran = 1; }
void Default_Handler(void) { for (;;) {} }

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [14] = PendSV_Handler, [15] = SysTick_Handler,
    [16 + 16] = TIM3_IRQHandler, [16 + 27] = USART1_IRQHandler,
};

void Reset_Handler(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; p++)
        *p = 0;
    REG(0x4002103C) |= 1u << 1;          /* RCC_APBENR1: TIM3 */
    REG(0x40021040) |= 1u << 14;         /* RCC_APBENR2: USART1 */
    REG(0x4001380C) = 556;               /* USART1: 115200 Bd at 64 MHz */
    REG(0x40013800) = 1u | 4u | 8u | 0x20u;   /* UE RE TE RXNEIE */
    REG(0xE000E410) = 0x40;              /* IPR4 byte 0 = IRQ16 (TIM3): priority 1 */
    REG(0xE000E100) = (1u << 16) | (1u << 27);
    REG(0x40000428) = 63;                /* TIM3: 10 kHz update interrupt */
    REG(0x4000042C) = 99;
    REG(0x4000040C) = 1;
    REG(0x40000400) = 1;
    REG(0xE000E014) = 63999;             /* SysTick 1 kHz */
    REG(0xE000E018) = 0;
    REG(0xE000E010) = 7;

    while (!tim_spin_ok) {
    }
    res[0] = tim_spin_ok;                /* 1: USART1 preempted TIM3 */

    __asm volatile("cpsid i");
    REG(0xE000ED04) = 1u << 28;          /* PENDSVSET while masked */
    for (volatile int i = 0; i < 100; i++) {
    }
    res[1] = pendsv_ran;                 /* 0: masked */
    __asm volatile("cpsie i");
    res[2] = pendsv_ran;                 /* 1: taken right after cpsie */

    uint32_t s0 = systick_count + tim_count;
    __asm volatile("wfi");
    res[3] = systick_count + tim_count - s0;   /* >= 1: woken by an interrupt */

    uint32_t t0 = systick_count;
    while (systick_count == t0) {        /* start on a SysTick edge */
    }
    t0 = systick_count;
    uint32_t k0 = tim_count;
    while (systick_count - t0 < 100u) {  /* 100 ms by SysTick */
    }
    res[4] = tim_count - k0;             /* ~1000 TIM3 ticks in 100 ms */
    res[5] = rx_byte;
    res[7] = 0xC0FFEEu;                  /* done */
    for (;;)
        __asm volatile("wfi");
}
