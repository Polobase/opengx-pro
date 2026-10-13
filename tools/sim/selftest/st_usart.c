/* USART FIFO mode on USART2 (tools/sim/selftest.py "usart", IRQ 28): TX FIFO depth, TXFE/TC
 * timing, RX FIFO full and overrun, one RXFNE interrupt per byte, sending from the TXFNF
 * interrupt. The harness sends "0123456789" at 2 ms and "abc" at 5 ms. */
#include "st.h"

#define RCC_APBENR1 0x4002103Cu
#define U2          0x40004400u
#define CR1         0x00u
#define BRR         0x0Cu
#define ISR         0x1Cu
#define ICR         0x20u
#define RDR         0x24u
#define TDR         0x28u
#define UE          (1u << 0)
#define RE          (1u << 2)
#define TE          (1u << 3)
#define RXFNEIE     (1u << 5)
#define TXFNFIE     (1u << 7)
#define FIFOEN      (1u << 29)
#define F_ORE       (1u << 3)
#define F_RXFNE     (1u << 5)
#define F_TC        (1u << 6)
#define F_TXFNF     (1u << 7)
#define F_TXFE      (1u << 23)
#define F_RXFF      (1u << 24)
#define SYST_CSR    0xE000E010u
#define SYST_RVR    0xE000E014u
#define SYST_CVR    0xE000E018u
#define NVIC_ISER   0xE000E100u

static volatile uint32_t rx_irqs, rx_word, tx_i;

void USART2_IRQHandler(void)
{
    uint32_t cr1 = REG(U2 + CR1), isr = REG(U2 + ISR);
    if ((cr1 & RXFNEIE) && (isr & F_RXFNE)) {   /* one byte per interrupt */
        rx_word |= (REG(U2 + RDR) & 0xFFu) << (8u * (rx_irqs & 3u));
        rx_irqs++;
    }
    if ((cr1 & TXFNFIE) && (isr & F_TXFNF)) {
        while ((REG(U2 + ISR) & F_TXFNF) && tx_i < 20u)
            REG8(U2 + TDR) = (uint8_t)('A' + tx_i++);
        if (tx_i == 20u)
            REG(U2 + CR1) = cr1 & ~TXFNFIE;
    }
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [16 + 28] = USART2_IRQHandler,
};

int main(void)
{
    REG(RCC_APBENR1) |= 1u << 17;               /* USART2 */
    REG(SYST_RVR) = 0xFFFFFFu;                  /* free-running down-counter at 64 MHz */
    REG(SYST_CVR) = 0;
    REG(SYST_CSR) = 5u;
    REG(U2 + BRR) = 556u;                       /* 115200 Bd: 5560 cycles per frame */
    REG(U2 + CR1) = FIFOEN | TE | RE | UE;

    /* 1. TX burst: the shift register takes one byte, the FIFO eight more */
    uint32_t t0 = REG(SYST_CVR), n = 0;
    while ((REG(U2 + ISR) & F_TXFNF) && n < 12u) {
        REG8(U2 + TDR) = (uint8_t)('0' + n);
        n++;
    }
    res[0] = n;
    res[1] = REG(U2 + ISR) & (F_TXFE | F_TC);
    while (!(REG(U2 + ISR) & F_TXFE)) {
    }
    res[2] = (t0 - REG(SYST_CVR)) & 0xFFFFFFu;
    while (!(REG(U2 + ISR) & F_TC)) {
    }
    res[3] = (t0 - REG(SYST_CVR)) & 0xFFFFFFu;

    /* 2. ten bytes arrive while nobody reads: eight stay, the ninth sets ORE */
    while (!(REG(U2 + ISR) & F_ORE))
        __asm volatile("wfi");
    res[4] = (REG(U2 + ISR) & F_RXFF) != 0;
    for (volatile uint32_t i = 0; i < 3000u; i++) {   /* the tenth byte arrives too */
    }
    uint32_t got = 0, w0 = 0, w1 = 0;
    while ((REG(U2 + ISR) & F_RXFNE) && got < 16u) {
        uint32_t c = REG(U2 + RDR) & 0xFFu;
        if (got < 4u)
            w0 |= c << (8u * got);
        else if (got < 8u)
            w1 |= c << (8u * (got - 4u));
        got++;
    }
    res[6] = w0;
    res[7] = w1;
    res[8] = got;
    REG(U2 + ICR) = F_ORE;                      /* else ORE keeps the RXFNE interrupt asserted */

    /* 3. three bytes with RXFNEIE: one interrupt per byte */
    REG(NVIC_ISER) = 1u << 28;
    REG(U2 + CR1) |= RXFNEIE;
    while (rx_irqs < 3u)
        __asm volatile("wfi");
    res[9] = rx_irqs;
    res[10] = rx_word;

    /* 4. twenty bytes sent from the TXFNF interrupt */
    REG(U2 + CR1) |= TXFNFIE;
    while (tx_i < 20u)
        __asm volatile("wfi");
    while (!(REG(U2 + ISR) & F_TC)) {
    }
    res[11] = tx_i;
    res[15] = DONE;
    return 0;
}
