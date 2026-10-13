/* nx time base (nx_time.h). */
#include "nx_time.h"

#include "nx_bl.h"
#include "nx_config.h"
#include "nx_diag.h"
#include "nx_hw.h"
#include "nx_keys.h"
#include "nx_runner.h"

volatile uint32_t g_nx_ms;

static uint8_t s_pb1;         /* debounced level */
static uint8_t s_pb1_new;     /* level being confirmed */
static uint8_t s_pb1_count;   /* consecutive samples of s_pb1_new */
static uint8_t s_key_div;

/* The original samples PB1 once per main-loop pass (13-27 ms apart), so it never sees short
 * glitches; sampling every millisecond needs a stable time instead, or a glitch would start an
 * extra PC-mode entry/exit sequence (docs/nx/design/core.md §2.4 item 6). */
static void pb1_sample(void)
{
    uint8_t raw = (uint8_t)nx_hw_pb1();
    if (raw == s_pb1) {
        s_pb1_count = 0;
        return;
    }
    if (raw != s_pb1_new) {
        s_pb1_new = raw;
        s_pb1_count = 0;
    }
    if (++s_pb1_count >= NX_PB1_DEBOUNCE_MS) {
        s_pb1 = raw;
        s_pb1_count = 0;
    }
}

void nx_tick_1ms(void)
{
    g_nx_ms++;
    nx_runner_tick_1ms();
    pb1_sample();
    if (++s_key_div >= NX_KEY_SAMPLE_MS) {
        s_key_div = 0;
        nx_keys_feed(nx_hw_keys(), nx_ms());
    }
    nx_bl_tick_1ms();
}

uint32_t nx_pb1(void)
{
    return s_pb1;
}

void nx_time_reset(void)
{
    g_nx_ms = 0;
    s_pb1 = s_pb1_new = (uint8_t)nx_hw_pb1();
    s_pb1_count = 0;
    s_key_div = 0;
}

#ifndef HOST_TEST
#include "stm32g0xx.h"

uint32_t nx_ms(void)
{
    return g_nx_ms;
}

uint16_t nx_us16(void)
{
    return (uint16_t)TIM6->CNT;
}

void nx_time_init(void)
{
    nx_time_reset();
    RCC->APBENR1 |= RCC_APBENR1_TIM6EN;
    TIM6->CR1 = 0;
    TIM6->PSC = 63;                      /* 64 MHz / 64 = 1 MHz */
    TIM6->ARR = 0xFFFFu;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->CR1 = TIM_CR1_CEN;

    SysTick->CTRL = 0;
    SysTick->LOAD = 64000u - 1u;         /* 1 kHz from the 64 MHz core clock */
    SysTick->VAL = 0;
    NVIC_SetPriority(SysTick_IRQn, 1);   /* below USART1/2 (0), docs/nx/fw2-design.md "IRQs" */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void)
{
    uint16_t t0 = nx_us16();
    nx_tick_1ms();
    nx_diag_isr_time((uint16_t)(nx_us16() - t0));
}
#endif
