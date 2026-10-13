/* nx UART driver (nx_uart.h). Device only; replaces src/classic/uart.c in APP=nx. */
#include "nx_uart.h"

#include "board.h"
#include "nx_diag.h"
#include "nx_time.h"
#include "nx_wdg.h"

#define RX_ERRORS (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)
#define RX_ERROR_CLEAR (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF)

static USART_TypeDef *const regs[UART_COUNT] = { USART1, USART2, USART3 };

struct tx_ring {
    uint8_t *buf;
    uint16_t mask;
    volatile uint16_t head;   /* written by the main loop */
    volatile uint16_t tail;   /* written by the interrupt */
};

static uint8_t s_buf_a[NX_UART_TX_SIZE], s_buf_b[NX_UART_TX_SIZE], s_buf_f[NX_UART3_TX_SIZE];
static struct tx_ring s_tx[UART_COUNT] = {
    { s_buf_a, NX_UART_TX_SIZE - 1u, 0, 0 },
    { s_buf_b, NX_UART_TX_SIZE - 1u, 0, 0 },
    { s_buf_f, NX_UART3_TX_SIZE - 1u, 0, 0 },
};

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
        u->ICR = RX_ERROR_CLEAR;
        if (i != UART_FACTORY)
            u->CR1 = USART_CR1_FIFOEN;   /* only while UE = 0; USART3 has no FIFO */
        u->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
        s_tx[i].head = s_tx[i].tail = 0;
    }
    NVIC_SetPriority(USART1_IRQn, 0);
    NVIC_SetPriority(USART2_IRQn, 0);
    NVIC_SetPriority(USART3_4_IRQn, 1);
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

static void irq(enum uart_port port)
{
    USART_TypeDef *u = regs[port];
    uint32_t isr = u->ISR;
    if (isr & RX_ERRORS) {
        if (isr & USART_ISR_ORE)
            g_nx_diag.uart_ore[port]++;
        if (isr & USART_ISR_FE)
            g_nx_diag.uart_fe[port]++;
        if (isr & USART_ISR_NE)
            g_nx_diag.uart_ne[port]++;
        u->ICR = RX_ERROR_CLEAR;
        g_uart_rx_errors[port]++;
    }
    while (isr & USART_ISR_RXNE_RXFNE) {     /* drain the FIFO: one hook call per byte, in order */
        uint8_t c = (uint8_t)u->RDR;
        g_uart_rx_bytes[port]++;
        if (!uart_rx_hook(port, c))
            g_uart_rx_errors[port]++;        /* nothing buffers bytes the hook refuses */
        isr = u->ISR;
    }
    if (u->CR1 & USART_CR1_TXEIE_TXFNFIE) {
        struct tx_ring *t = &s_tx[port];
        while ((u->ISR & USART_ISR_TXE_TXFNF) && t->tail != t->head) {
            u->TDR = t->buf[t->tail];
            t->tail = (uint16_t)((t->tail + 1u) & t->mask);
        }
        if (t->tail == t->head)
            u->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
}

void USART1_IRQHandler(void)
{
    uint16_t t0 = nx_us16();
    irq(UART_GEN_A);
    nx_diag_isr_time((uint16_t)(nx_us16() - t0));
}

void USART2_IRQHandler(void)
{
    uint16_t t0 = nx_us16();
    irq(UART_GEN_B);
    nx_diag_isr_time((uint16_t)(nx_us16() - t0));
}

void USART3_4_IRQHandler(void)
{
    irq(UART_FACTORY);
}

int uart_getc(enum uart_port port)
{
    (void)port;
    return -1;
}

unsigned uart_tx_room(enum uart_port port)
{
    const struct tx_ring *t = &s_tx[port];
    return (unsigned)((t->tail - t->head - 1u) & t->mask);
}

int uart_tx_idle(enum uart_port port)
{
    const struct tx_ring *t = &s_tx[port];
    return t->tail == t->head && (regs[port]->ISR & USART_ISR_TC);
}

void uart_putc(enum uart_port port, uint8_t c)
{
    struct tx_ring *t = &s_tx[port];
    uint16_t next = (uint16_t)((t->head + 1u) & t->mask);
    if (next == t->tail) {
        uint32_t t0 = nx_ms();
        while (next == t->tail) {
            if (nx_ms() - t0 > NX_UART_WAIT_MS) {
                g_nx_diag.uart_tx_timeouts[port]++;
                return;
            }
            nx_wdg_feed();
        }
    }
    t->buf[t->head] = c;
    t->head = next;
    g_uart_tx_bytes[port]++;
    regs[port]->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

void uart_puts(enum uart_port port, const char *s)
{
    while (*s)
        uart_putc(port, (uint8_t)*s++);
}

void uart_flush(enum uart_port port)
{
    uint32_t t0 = nx_ms();
    while (!uart_tx_idle(port)) {
        if (nx_ms() - t0 > NX_UART_WAIT_MS) {
            g_nx_diag.uart_tx_timeouts[port]++;
            return;
        }
        nx_wdg_feed();
    }
}
