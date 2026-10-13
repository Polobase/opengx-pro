/* nx time base (docs/nx/fw2-design.md, "Time"): SysTick at 1 kHz counts milliseconds and runs
 * the periodic work (runner time, key sampling, PB1 debounce, backlight); TIM6 runs free at 1 MHz
 * for microsecond stamps. */
#pragma once

#include <stdint.h>

#define NX_KEY_SAMPLE_MS 10u   /* key sampling period (docs/nx/design/ui.md §5.1) */

extern volatile uint32_t g_nx_ms;   /* SysTick count since nx_time_init */

uint32_t nx_ms(void);      /* milliseconds since boot (host tests: the simulated clock) */
uint16_t nx_us16(void);    /* free-running microseconds, wraps every 65.536 ms: short intervals only */

void nx_time_init(void);   /* device: SysTick 1 kHz (priority 1) and TIM6; call before __enable_irq */
void nx_time_reset(void);  /* host tests: counters back to 0, PB1 state from nx_hw_pb1() */
/* The SysTick handler's work, once per millisecond (the host tests call it from the simulated
 * clock): runner time, PB1 debounce, key sampling every NX_KEY_SAMPLE_MS, backlight. */
void nx_tick_1ms(void);

uint32_t nx_pb1(void);     /* PB1 after NX_PB1_DEBOUNCE_MS of stable level: 1 = PC connected */
