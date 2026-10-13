/* Error and health counters of the nx platform (docs/nx/design/core.md §5). Written by the
 * modules that detect the condition, read by the host tests and the Diagnostics screen. Counters written in an interrupt handler are only incremented
 * there. */
#pragma once

#include <stdint.h>

#include "nx_vm.h"
#include "uart.h"

struct nx_diag {
    uint32_t uart_ore[UART_COUNT];     /* receive overrun per port (USART1, USART2, USART3) */
    uint32_t uart_fe[UART_COUNT];      /* framing error */
    uint32_t uart_ne[UART_COUNT];      /* noise error */
    uint32_t uart_tx_timeouts[UART_COUNT];   /* a transmit wait hit its bound (byte dropped) */
    uint32_t rx_dropped[2];            /* bytes the parser dropped because a line was pending */
    uint32_t ok_timeouts[2];           /* generator lines without :ok in NX_TXQ_ACK_TIMEOUT_MS */
    uint32_t txq_rejected_pc;          /* generator lines refused in PC mode: must stay 0 */
    uint32_t flash_timeouts;           /* SPI-flash busy waits that hit their bound */
    uint32_t flash_quiesce_waits;      /* legacy SPI-flash calls that found the chip busy */
    uint32_t flash_guard_refusals;     /* nx writes outside 0x080000-0x1FFFFF refused */
    uint32_t flash_verify_errors;      /* nx writes whose read-back differed */
    uint32_t loop_gap_max_ms;          /* longest time between two main-loop passes */
    uint32_t pc_entries, pc_exits;     /* PC-mode entries / exits (PB1 changes acted on) */
    uint32_t w91_held;                 /* :w91 lines held after a :w92 (nx_loop.c) */
    uint32_t reset_flags;              /* RCC_CSR at boot (reset cause flags) */
    uint16_t isr_max_us;               /* longest SysTick / USART1 / USART2 handler */
    uint8_t reset_reason;              /* enum nx_reset (nx_fault.h) */
    uint8_t safe_mode;                 /* 1: boot-loop guard tripped (NX_BOOTLOOP_N abnormal resets) */
    uint8_t crc_hw;                    /* 1: the CRC unit passed its self-test and is used */
    char panel;                        /* LCD init variant 'A' / 'B' (main_nx.c, for the About page) */
};

extern struct nx_diag g_nx_diag;

/* PC monitor: what the PC set that the protocol core does not keep, read from the receive tap
 * (the bytes of USART1/2 before link.c; observe only). Written in the receive interrupt, cleared
 * at each PC-mode entry.
 *
 * cycles: Spooky2 uploads the generator's wave tables (":aNN=" + 1024 samples of 0..1023) and,
 * with a Wave Cycle Multiplier, puts several cycles into one table and sends the frequency
 * divided by their number. The tap looks at a table's upward crossings of 512 (circular):
 * - evenly spaced ones (the gaps between them, the one around the table's end too, within
 *   NX_PCMON_GAP_TOL samples of each other) are its cycles: sine, square, the damped shapes;
 * - NX_PCMON_GRID_MIN or more that all lie on one grid of c steps around the table are c
 *   cycles: Spooky2's H-Bomb tables are 16 cycle slots of which 8 carry a wave and 8 are flat;
 * - anything else counts as 1. */
#define NX_PCMON_TAB0 11u      /* the first table kept: Spooky2 uploads 11..25 */
#define NX_PCMON_TABS 16u
#define NX_PCMON_SAMPLES 1024u
#define NX_PCMON_GAP_TOL 2u
#define NX_PCMON_MAX_CYCLES 180u   /* more counts as 1 (and 99999999.99999999 Hz times it fits 1.8e18) */
#define NX_PCMON_GAPS 64u      /* gaps kept for the grid rule: a table with more crossings must be even */
#define NX_PCMON_GRID_MIN 3u   /* two crossings fit a grid by chance too easily */

struct nx_pcmon {
    uint8_t wave[2][2];      /* [ch][out]: w20 / w21 */
    uint16_t ampl_cv[2][2];  /* w28 / w29, 0.01 V peak to peak */
    uint8_t gate;            /* bit ch: w12 with a first value != 0 (per-step gating on) */
    uint8_t seen[2];         /* NX_PCS_* (nx_vm.h) per ch */
    uint8_t cycles[2][NX_PCMON_TABS];   /* [ch][table - NX_PCMON_TAB0]: 0 = not uploaded this session */
};
extern struct nx_pcmon g_nx_pcmon;

void nx_diag_reset(void);   /* all counters 0 (boot, host tests) */
/* LINK_RX_TAP: every byte of USART1/2 before the link parser (receive interrupt). */
void nx_rx_tap(unsigned ch, uint8_t c);
/* Main loop, once per pass: tracks the longest gap between passes. */
void nx_diag_loop(uint32_t now_ms);
/* Handler time in us (from nx_us16 stamps); keeps the maximum. */
void nx_diag_isr_time(uint16_t us);
/* MCU temperature (0.1 degC) and supply VDDA (mV) from raw ADC readings of the temperature sensor
 * and VREFINT and their factory calibration (taken at 30 degC and 3.0 V; datasheet average slope
 * 2.5 mV/degC, not checked against a thermometer). Returns 0 (nothing written) when vref is 0. */
int nx_diag_sensors(uint16_t ts, uint16_t vref, uint16_t ts_cal, uint16_t vref_cal, int16_t *t10, uint16_t *vdd_mv);
