/* nx UART driver: the uart.h API (so link.c and factory.c run unchanged) on USART1/2 with their
 * 8-byte FIFOs and on USART3, with interrupt-driven transmit rings (docs/nx/design/core.md §4).
 *
 * The receive interrupt drains the FIFO and hands every byte to uart_rx_hook() (link.c consumes
 * all of them, so there is no receive ring). uart_putc() only queues a byte; uart_flush() still
 * returns when the last byte has left the wire (ring empty and TC), exactly like the classic
 * polled driver: a reply is on the wire before link.c clears the line-pending flag. Waits feed
 * the watchdog and give up after NX_UART_WAIT_MS (counted in g_nx_diag.uart_tx_timeouts).
 *
 * Extensions for the non-blocking generator queue (nx_txq.c); the host tests implement them in
 * tests/host/nx/nx_world.c. */
#pragma once

#include "uart.h"

#define NX_UART_TX_SIZE 512u    /* USART1/2 transmit ring (power of two) */
#define NX_UART3_TX_SIZE 32u    /* USART3: single 'Z' replies */
#define NX_UART_WAIT_MS 100u    /* 512 bytes take 44 ms at 115200 baud */

unsigned uart_tx_room(enum uart_port port);   /* bytes uart_putc() can queue without waiting */
int uart_tx_idle(enum uart_port port);        /* 1 when nothing is queued and the last frame has left */
