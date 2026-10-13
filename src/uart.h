/* USART1/2/3 at 115200 8N1 (docs/spec/hardware.md §4.1): interrupt-driven receive into ring
 * buffers, blocking transmit. Unlike the original, receive errors (overrun, framing, noise,
 * parity) are cleared, so an overrun cannot stall the firmware. */
#pragma once

#include <stdint.h>

enum uart_port {
    UART_GEN_A = 0,     /* USART1, PA9/PA10: generator board A */
    UART_GEN_B = 1,     /* USART2, PA2/PA3: generator board B */
    UART_FACTORY = 2,   /* USART3, PB8/PB9: factory/calibration connector */
    UART_COUNT = 3,
};

extern volatile uint32_t g_uart_rx_bytes[UART_COUNT];
extern volatile uint32_t g_uart_rx_errors[UART_COUNT];
extern uint32_t g_uart_tx_bytes[UART_COUNT];

void uart_init(void);
/* Called from the receive interrupt for every byte. Return nonzero if the byte was consumed;
 * otherwise it goes into the ring buffer for uart_getc(). Default: never consumes. */
int uart_rx_hook(enum uart_port port, uint8_t c);
int uart_getc(enum uart_port port);            /* -1 when nothing is buffered */
void uart_putc(enum uart_port port, uint8_t c);
void uart_puts(enum uart_port port, const char *s);
void uart_flush(enum uart_port port);          /* wait until the last byte has left the wire */
