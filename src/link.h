/* Generator links: USART1 = G1 (board A), USART2 = G2 (board B), ':' protocol, plus the
 * USART3 factory port. Behaviour: docs/spec/protocols.md, docs/spec/application.md §7, §9.3.
 * Channel index ch: 0 = G1/USART1, 1 = G2/USART2. */
#pragma once

#include <stdint.h>

void link_init(void);

/* Receive side: link.c implements uart_rx_hook() (uart.h) and parses every byte in interrupt
 * context like the original ISRs (0x0800C758, 0x0800CF90, 0x0800D7D8): mirrored :w values and
 * g/p uploads take effect immediately; frames needing main-loop work set UIF_LINE(ch) in
 * g_ui_flags, USART3 commands set bit0 of g_factory_cmd_flags. */

/* Main loop: handle the pending frame of channel ch (0x0800F22C / 0x0800F9C4), clears UIF_LINE(ch). */
void link_process(unsigned ch);
/* Main loop: execute the pending USART3 factory command (factory_cmd_execute 0x0800522C). */
void link_factory_execute(void);

/* Transmit (protocols.md §5.1). All block until the bytes are in the UART. */
void link_send_line(unsigned ch, const char *s);                          /* s + "\r\n" */
void link_send_w_u64(unsigned ch, unsigned id, uint64_t v);               /* ":wNN=<v>,\r\n" */
void link_send_w_pair5(unsigned ch, unsigned id, unsigned a, unsigned b); /* ":wNN=aaaaa,bbbbb,\r\n" */
void link_send_cal_value(unsigned idx, unsigned v);                       /* ":w60=1112111,II,VVVVV,\r\n" on USART1 */
/* gen_wait_ok 0x08012524: wait for ":ok" from either link (shared flag, original behaviour) or
 * the original's timeout, then the short trailing delay. */
void link_wait_ok(void);

/* Transmit prebuilt bytes (for example a runner_seq line) and wait until they are on the wire. */
void link_send_raw(unsigned ch, const char *buf, unsigned len);
/* Pure builders of the lines above; return the length including "\r\n".
 * link_fmt_w_u64 needs 28 bytes of buf, link_fmt_w_pair5 20. */
unsigned link_fmt_w_u64(char *buf, unsigned id, uint64_t v);
unsigned link_fmt_w_pair5(char *buf, unsigned id, unsigned a, unsigned b);
/* 1 while link ch's parser waits for the next ':' frame (between frames, or after a complete
 * line that is still pending). */
int link_rx_idle(unsigned ch);

/* Extension hooks for the nx firmware, defined on the compiler command line, e.g.
 * -DLINK_RX_TAP=nx_rx_tap -DLINK_OK_HOOK=nx_ok_hook; undefined in the classic apps.
 *   LINK_RX_TAP(ch, c)  receive interrupt: sees every byte of USART1/2 before the parser, which
 *                       still receives every byte unchanged (so ':' traffic is not affected)
 *   LINK_OK_HOOK(ch)    receive interrupt: ":ok" recognised on link ch */

/* Calibration test signals (protocols.md §5.4): cal_apply_test 0x08011468 (menu, uses
 * g_cal_index / g_cal_freq_exp / g_cal_ampl_exp) and cal_apply_test_jig 0x08011C2C (USART3 ':b'). */
void cal_apply_test(void);
void cal_apply_test_jig(void);
