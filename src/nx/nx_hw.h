/* Board access of the portable nx modules. The device implementation is src/nx/nx_hw.c; the host
 * tests implement the same functions in tests/host/nx/nx_world.c, so every nx module except the
 * drivers (nx_hw.c, nx_uart.c, nx_wdg.c) builds and runs on the host. */
#pragma once

#include <stdint.h>

/* Key bits of the raw key mask (1 = pressed), in the order of docs/spec/application.md §3.1. */
#define NX_KEY_LOOP  0x01u   /* PD0 */
#define NX_KEY_START 0x02u   /* PD1 */
#define NX_KEY_STOP  0x04u   /* PB5 */
#define NX_KEY_UP    0x08u   /* PB4 */
#define NX_KEY_DOWN  0x10u   /* PD3 */
#define NX_KEY_G1    0x20u   /* PB3 */
#define NX_KEY_G2    0x40u   /* PD2 */
#define NX_KEY_POWER 0x80u   /* PB11 */

uint8_t nx_hw_keys(void);      /* the eight keys now, NX_KEY_* bits */
uint32_t nx_hw_pb1(void);      /* PB1 now: 1 = PC connected */
/* Backlight output: on_us of every period_us high. on_us == 0: PB12 low, on_us >= period_us:
 * PB12 high, both without interrupts; otherwise the TIM17 two-phase PWM. */
void nx_hw_bl_set(uint16_t on_us, uint16_t period_us);
void nx_hw_uid(uint8_t out[12]);   /* the MCU's 96-bit unique ID */
/* Internal ADC channels (NX_ADC_TEMP, NX_ADC_VREF): the raw 12-bit reading, 0 when the ADC is
 * not ready. The first call starts the ADC (about 100 us); Diagnostics reads it once a second. */
#define NX_ADC_TEMP 12u   /* temperature sensor */
#define NX_ADC_VREF 13u   /* VREFINT */
uint16_t nx_hw_adc(unsigned channel);
/* Factory calibration in system memory: TS_CAL1 (sensor at 30 degC) and VREFINT_CAL, both taken
 * at VDDA = 3.0 V. */
void nx_hw_adc_cal(uint16_t *ts_cal, uint16_t *vref_cal);
