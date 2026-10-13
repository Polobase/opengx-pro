#include "uart.h"

#include "board.h"

#define RX_SIZE 512u   /* power of two; ~44 ms of continuous data at 115200 baud */

static USART_TypeDef *const regs[UART_COUNT] = { USART1, USART2, USART3 };

static struct {
    uint8_t buf[RX_SIZE];
    volatile uint16_t head;   /* written by the IRQ */
    uint16_t tail;            /* written by the main loop */
} rx[UART_COUNT];

volatile uint32_t g_uart_rx_bytes[UART_COUNT];
volatile uint32_t g_uart_rx_errors[UART_COUNT];
uint32_t g_uart_tx_bytes[UART_COUNT];

void uart_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    RCC->APBENR1 |= RCC_APBENR1_USART2EN | RCC_APBENR1_USART3EN;
    for (unsigned i = 0; i < UART_COUNT; i++) {
        USART_TypeDef *u = regs[i];
        u->CR1 = 0;
        u->BRR = 556;   /* 64 MHz / 115200, same value as the original */
        u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
        rx[i].head = rx[i].tail = 0;
    }
    NVIC_SetPriority(USART1_IRQn, 0);
    NVIC_SetPriority(USART2_IRQn, 0);
    NVIC_SetPriority(USART3_4_IRQn, 0);
    NVIC_EnableIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART2_IRQn);
    NVIC_EnableIRQ(USART3_4_IRQn);
}

__attribute__((weak)) int uart_rx_hook(enum uart_port port, uint8_t c)
{
    (void)port;
    (void)c;
    return 0;
}

static void rx_irq(enum uart_port port)
{
    USART_TypeDef *u = regs[port];
    uint32_t isr = u->ISR;
    /* DEVIATION: receive errors are cleared; the original never clears them, so an overrun
     * could leave its interrupt re-triggering and stall the main loop. */
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        g_uart_rx_errors[port]++;
    }
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)u->RDR;
        g_uart_rx_bytes[port]++;
        if (uart_rx_hook(port, c))
            return;
        uint16_t next = (uint16_t)((rx[port].head + 1u) & (RX_SIZE - 1u));
        if (next != rx[port].tail) {
            rx[port].buf[rx[port].head] = c;
            rx[port].head = next;
        } else {
            g_uart_rx_errors[port]++;   /* buffer full: byte dropped */
        }
    }
}

void USART1_IRQHandler(void)
{
    rx_irq(UART_GEN_A);
}

void USART2_IRQHandler(void)
{
    rx_irq(UART_GEN_B);
}

void USART3_4_IRQHandler(void)
{
    rx_irq(UART_FACTORY);
}

int uart_getc(enum uart_port port)
{
    if (rx[port].tail == rx[port].head)
        return -1;
    uint8_t c = rx[port].buf[rx[port].tail];
    rx[port].tail = (uint16_t)((rx[port].tail + 1u) & (RX_SIZE - 1u));
    return c;
}

void uart_putc(enum uart_port port, uint8_t c)
{
    USART_TypeDef *u = regs[port];
    while (!(u->ISR & USART_ISR_TXE_TXFNF)) {
    }
    u->TDR = c;
    g_uart_tx_bytes[port]++;
}

void uart_puts(enum uart_port port, const char *s)
{
    while (*s)
        uart_putc(port, (uint8_t)*s++);
}

void uart_flush(enum uart_port port)
{
    while (!(regs[port]->ISR & USART_ISR_TC)) {
    }
}
