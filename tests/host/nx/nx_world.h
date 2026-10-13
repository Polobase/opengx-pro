/* Host model of the board around the nx firmware (tests/host/nx/): the simulated clock drives
 * the SysTick work (nx_tick_1ms) every millisecond; keys, PB1, received bytes and the backlight
 * output are modelled here. It implements nx_hw.h and nx_ms()/nx_us16() for the nx modules.
 * Every nx_wdg_feed() takes 5 us of simulated time, so the firmware's bounded busy waits make
 * progress on the host.
 * The UART transmit time (86.8 us per byte at 115200 baud) and generator boards that answer
 * ":w" lines with ":ok" are modelled for the nx_uart.h extensions (nx_txq).
 * power_off_signal() jumps back into the test (nxw_power_off_jmp).
 */
#pragma once

#include <setjmp.h>
#include <stdint.h>

enum { NXW_ACK = 0, NXW_NOACK = 1, NXW_NOTABLE = 2 };   /* NXW_NOTABLE: ":w" lines answered, ":a" tables not */

extern uint8_t nxw_keys;             /* raw key mask the firmware reads (NX_KEY_* bits) */
extern uint32_t nxw_pb1;             /* PB1 level */
extern uint16_t nxw_bl_on_us, nxw_bl_period_us;   /* last nx_hw_bl_set() */
extern unsigned nxw_bl_calls;
extern unsigned nxw_wdg_feeds;
extern uint64_t nxw_line_us[2][64];  /* start time of every line sent on link ch */
extern uint64_t nxw_line_end_us[2][64];   /* ... and when its last byte left the wire */
extern unsigned nxw_line_n[2];
extern uint32_t nxw_table_ack_us;    /* the generators answer a wave table (":a...") this long after its line */
extern jmp_buf nxw_power_off_jmp;    /* power_off_signal() jumps here once armed */
extern int nxw_power_off_armed;
extern uint64_t nxw_power_off_us;    /* simulated time of the power-off signal */

/* Everything back to power-on: harness mocks, W25Q16 model, clock, firmware state (g_ch = the
 * empty program, ST_BOOT) and the nx modules. */
void nxw_reset(void);
void nxw_rx(unsigned ch, const char *s);                /* bytes arrive on link ch now */
void nxw_rx_at(uint64_t t_us, unsigned ch, const char *s);   /* ... at simulated time t_us */
void nxw_keys_at(uint64_t t_us, uint8_t keys);          /* nxw_keys = keys at simulated time t_us */
void nxw_poll(void);                 /* deliver what is due (received bytes, key changes) */
void nxw_gen(unsigned ch, int mode, uint32_t ack_us);   /* generator model of link ch (NXW_ACK: :ok after ack_us) */
/* The bytes of line n (0-based) sent on link ch since the last nxw_clear(), without "\r\n", into
 * out (cap bytes, NUL-terminated); returns its full length, 0 when there is no such line. */
unsigned nxw_line(unsigned ch, unsigned n, char *out, unsigned cap);
void nxw_tx_model(int on);           /* 0: detach the transmit-time/generator model (classic runs) */
void nxw_clear(void);                /* tx_clear() and the line times */
/* Lines sent on link ch (tx_log of mocks.c) since the last nxw_clear(), without "\r\n". */
unsigned nxw_lines(unsigned ch, char lines[][32], unsigned max);
void nxw_step(uint32_t us);          /* one main-loop pass, then the clock advances by us */
void nxw_run_ms(uint32_t ms);        /* main-loop passes every 50 us for ms of simulated time */
void nxw_run_until(uint32_t t_ms);   /* ... until sim_ms() == t_ms */
/* Holds keys for hold_ms while the main loop runs, then releases them and runs 100 ms more. */
void nxw_press(uint8_t keys, uint32_t hold_ms);
