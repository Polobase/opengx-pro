/* Board access of the portable nx modules (nx_hw.h), device side. The TIM17 backlight driver
 * (nx_hw_bl_set) is in nx_bl.c next to its interrupt handler. */
#include "nx_hw.h"

#include <string.h>

#include "board.h"
#include "nx_time.h"

uint8_t nx_hw_keys(void)
{
    uint32_t b = ~GPIOB->IDR, d = ~GPIOD->IDR;   /* active low with pull-ups (keys.c) */
    uint8_t m = 0;
    if (d & (1u << 0))
        m |= NX_KEY_LOOP;
    if (d & (1u << 1))
        m |= NX_KEY_START;
    if (b & (1u << 5))
        m |= NX_KEY_STOP;
    if (b & (1u << 4))
        m |= NX_KEY_UP;
    if (d & (1u << 3))
        m |= NX_KEY_DOWN;
    if (b & (1u << 3))
        m |= NX_KEY_G1;
    if (d & (1u << 2))
        m |= NX_KEY_G2;
    if (b & (1u << 11))
        m |= NX_KEY_POWER;
    return m;
}

uint32_t nx_hw_pb1(void)
{
    return pin_read(GPIOB, HOST_DETECT_PIN);
}

void nx_hw_uid(uint8_t out[12])
{
    memcpy(out, (const void *)UID_BASE, 12);
}

/* ---- ADC: temperature sensor and VREFINT (as src/classic/main_nxbring.c, checked on the unit) ---- */

#define TS_CAL1 (*(const volatile uint16_t *)0x1FFF75A8u)
#define VREF_CAL (*(const volatile uint16_t *)0x1FFF75AAu)

static uint8_t s_adc;   /* 0: not started, 1: ready, 2: failed */

static int adc_wait(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t n = 0; n < 20000u; n++)
        if ((*reg & mask) == want)
            return 1;
    return 0;
}

static void adc_delay_us(uint16_t us)
{
    uint16_t t0 = nx_us16();
    while ((uint16_t)(nx_us16() - t0) < us) {
    }
}

static void adc_start(void)
{
    s_adc = 2;
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_0;   /* PCLK/2 = 32 MHz (max 35 MHz) */
    ADC1->CR = ADC_CR_ADVREGEN;
    adc_delay_us(30);                   /* > 20 us regulator start-up */
    ADC1->CR |= ADC_CR_ADCAL;
    if (!adc_wait(&ADC1->CR, ADC_CR_ADCAL, 0))
        return;
    ADC1->SMPR = 7u;                    /* 160.5 cycles = 5 us: the sensor needs >= 5 us */
    ADC1_COMMON->CCR |= ADC_CCR_TSEN | ADC_CCR_VREFEN;
    adc_delay_us(30);                   /* sensor start-up (10 us) */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    if (adc_wait(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY))
        s_adc = 1;
}

uint16_t nx_hw_adc(unsigned channel)
{
    if (!s_adc)
        adc_start();
    if (s_adc != 1)
        return 0;
    ADC1->ISR = ADC_ISR_CCRDY;
    ADC1->CHSELR = 1u << channel;
    if (!adc_wait(&ADC1->ISR, ADC_ISR_CCRDY, ADC_ISR_CCRDY))
        return 0;
    ADC1->CR |= ADC_CR_ADSTART;
    if (!adc_wait(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC))
        return 0;
    return (uint16_t)ADC1->DR;
}

void nx_hw_adc_cal(uint16_t *ts_cal, uint16_t *vref_cal)
{
    *ts_cal = TS_CAL1;
    *vref_cal = VREF_CAL;
}
