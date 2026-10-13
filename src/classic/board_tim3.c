/* Classic time base (original TIM3_IRQHandler 0x0800C604): TIM3 at 10 kHz drives g_ticks,
 * g_tick8, the 16-step soft PWM of the backlight (PB12) and PB7, and the application's
 * tick_isr_hook. Linked by the classic apps; the nx firmware has its own time base. */
#include "board.h"

volatile uint32_t g_ticks;
volatile uint8_t g_tick8;
volatile uint8_t g_backlight_duty;
volatile uint8_t g_pb7_duty;

void board_timebase_init(void)
{
    g_pb7_duty = 1;   /* original value after init */
    RCC->APBENR1 |= RCC_APBENR1_TIM3EN;
    TIM3->PSC = 63;
    TIM3->ARR = 99;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1 |= TIM_CR1_CEN;
    NVIC_SetPriority(TIM3_IRQn, 0);   /* same as the UARTs, like the original: no mutual preemption */
    NVIC_EnableIRQ(TIM3_IRQn);
}

__attribute__((weak)) void tick_isr_hook(void)
{
}

/* Same order and arithmetic as the original TIM3_IRQHandler 0x0800C604 for the PWM part. */
void TIM3_IRQHandler(void)
{
    TIM3->SR = ~TIM_SR_UIF;
    g_ticks++;
    uint8_t phase = (uint8_t)(++g_tick8 & 15u);
    GPIOB->BSRR = (phase < g_pb7_duty ? 1u << PB7_PWM_PIN : 1u << (PB7_PWM_PIN + 16u))
                | (phase < g_backlight_duty ? 1u << BACKLIGHT_PIN : 1u << (BACKLIGHT_PIN + 16u));
    tick_isr_hook();
}

void delay_ms(uint32_t ms)
{
    uint32_t start = g_ticks;
    while (g_ticks - start < ms * 10u) {
    }
}
