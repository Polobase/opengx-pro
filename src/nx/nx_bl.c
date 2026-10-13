/* Backlight and PB7 outputs (nx_bl.h). The level logic runs in the SysTick handler and is the
 * only caller of nx_hw_bl_set(), so the TIM17 registers are written from one context only. */
#include "nx_bl.h"

#include "nx_config.h"
#include "nx_hw.h"

volatile uint8_t g_backlight_duty;
volatile uint8_t g_pb7_duty;

static uint8_t s_user = NX_BL_DEFAULT_LEVEL;
static uint8_t s_level;        /* level of the fade (the output unless w64 holds it off) */
static uint8_t s_target, s_fade_start;
static uint16_t s_fade_ms, s_fade_t;
static uint8_t s_host_off;
static uint8_t s_applied;      /* output level last given to the hardware */
static volatile uint8_t s_req, s_req_level;
static volatile uint16_t s_req_ms;

uint16_t nx_bl_on_us(uint8_t level)
{
    if (level == 0)
        return 0;
    if (level >= NX_BL_LEVEL_MAX)
        return NX_BL_PERIOD_US;
    uint32_t on = (uint32_t)level * level * NX_BL_PERIOD_US / (NX_BL_LEVEL_MAX * NX_BL_LEVEL_MAX);
    if (on < NX_BL_MIN_PHASE_US)
        on = NX_BL_MIN_PHASE_US;
    if (on > NX_BL_PERIOD_US - NX_BL_MIN_PHASE_US)
        on = NX_BL_PERIOD_US - NX_BL_MIN_PHASE_US;
    return (uint16_t)on;
}

void nx_bl_fade_to(uint8_t level, uint16_t ms)
{
    s_req_level = level;
    s_req_ms = ms;
    s_req = 1;   /* picked up by the next tick, which owns the fade state */
}

void nx_bl_set_user(uint8_t level)
{
    s_user = level;
    nx_bl_fade_to(level, 0);
}

uint8_t nx_bl_user(void)
{
    return s_user;
}

uint8_t nx_bl_level(void)
{
    return s_host_off ? 0 : s_level;
}

int nx_bl_host_off(void)
{
    return s_host_off;
}

static volatile uint8_t s_w64_on;

int nx_bl_w64_on_event(void)
{
    if (!s_w64_on)
        return 0;
    s_w64_on = 0;
    return 1;
}

void nx_bl_host_release(void)
{
    s_host_off = 0;
}

void nx_bl_tick_1ms(void)
{
    uint8_t duty = g_backlight_duty;
    if (duty != NX_BL_W64_NONE) {          /* w64 (link.c): 0 = off, 8 = on at the user's level */
        g_backlight_duty = NX_BL_W64_NONE;   /* consumed: a second w64 on lights it again */
        s_host_off = duty == 0;
        if (!s_host_off) {
            s_w64_on = 1;
            s_level = s_target = s_user;
            s_fade_ms = s_fade_t = 0;
        }
    }
    if (s_req) {
        s_req = 0;
        s_fade_start = s_level;
        s_target = s_req_level;
        s_fade_ms = s_req_ms;
        s_fade_t = 0;
    }
    if (s_fade_t < s_fade_ms) {
        s_fade_t++;
        int32_t d = (int32_t)s_target - (int32_t)s_fade_start;
        s_level = (uint8_t)((int32_t)s_fade_start + d * (int32_t)s_fade_t / (int32_t)s_fade_ms);
    } else {
        s_level = s_target;
    }
    uint8_t out = s_host_off ? 0 : s_level;
    if (out != s_applied) {
        s_applied = out;
        nx_hw_bl_set(nx_bl_on_us(out), NX_BL_PERIOD_US);
    }
}

#ifdef HOST_TEST
void nx_bl_init(void)
{
    g_backlight_duty = NX_BL_W64_NONE;
    g_pb7_duty = 1;
    s_user = NX_BL_DEFAULT_LEVEL;
    s_level = s_target = s_fade_start = 0;
    s_fade_ms = s_fade_t = 0;
    s_host_off = 0;
    s_applied = 0;
    s_req = 0;
}
#else
#include "board.h"

static volatile uint16_t s_on_us, s_off_us;
static uint8_t s_phase_high, s_running;

void nx_bl_init(void)
{
    g_backlight_duty = NX_BL_W64_NONE;   /* no w64 pending; the UI turns the backlight on */
    g_pb7_duty = 1;         /* the original's PB7 duty after init */

    RCC->APBENR2 |= RCC_APBENR2_TIM17EN;
    TIM17->CR1 = 0;
    TIM17->PSC = 63;        /* 1 MHz */
    TIM17->ARR = 999;
    TIM17->EGR = TIM_EGR_UG;
    TIM17->SR = 0;
    NVIC_SetPriority(TIM17_IRQn, 1);
    NVIC_EnableIRQ(TIM17_IRQn);

    RCC->APBENR1 |= RCC_APBENR1_TIM3EN;
    TIM3->CR1 = 0;
    TIM3->PSC = 63;         /* 1 MHz */
    TIM3->ARR = 1599;       /* 625 Hz, the original's soft-PWM frequency (10 kHz / 16) */
    TIM3->CCR2 = 100;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->DIER = TIM_DIER_UIE | TIM_DIER_CC2IE;
    TIM3->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM3_IRQn, 1);
    NVIC_EnableIRQ(TIM3_IRQn);
}

void nx_hw_bl_set(uint16_t on_us, uint16_t period_us)
{
    if (on_us == 0 || on_us >= period_us) {   /* static level: no interrupts */
        TIM17->CR1 = 0;
        TIM17->DIER = 0;
        TIM17->SR = 0;
        NVIC_ClearPendingIRQ(TIM17_IRQn);
        s_running = 0;
        if (on_us)
            pin_high(GPIOB, BACKLIGHT_PIN);
        else
            pin_low(GPIOB, BACKLIGHT_PIN);
        return;
    }
    s_on_us = on_us;                          /* taken over at the next phase change */
    s_off_us = (uint16_t)(period_us - on_us);
    if (!s_running) {
        s_running = 1;
        s_phase_high = 1;
        pin_high(GPIOB, BACKLIGHT_PIN);
        TIM17->ARR = (uint16_t)(on_us - 1u);
        TIM17->CNT = 0;
        TIM17->SR = 0;
        TIM17->DIER = TIM_DIER_UIE;
        TIM17->CR1 = TIM_CR1_CEN;             /* ARPE = 0: a new ARR applies to the running cycle */
    }
}

void TIM17_IRQHandler(void)
{
    TIM17->SR = ~TIM_SR_UIF;
    uint16_t arr;
    if (s_phase_high) {
        pin_low(GPIOB, BACKLIGHT_PIN);
        arr = (uint16_t)(s_off_us - 1u);
        s_phase_high = 0;
    } else {
        pin_high(GPIOB, BACKLIGHT_PIN);
        arr = (uint16_t)(s_on_us - 1u);
        s_phase_high = 1;
    }
    TIM17->ARR = arr;   /* ARPE = 0: applies to the running cycle */
    /* ISR latency (SysTick's key sampling, USART1/2 at a higher priority) can already have taken
     * longer than a short phase by the time ARR is written here: CNT has then already passed it,
     * so the counter would free-run to 0xFFFF before wrapping, stretching that phase to ~65.5 ms
     * instead of a few us. Force the update instead: CNT resets to 0 and
     * this handler re-enters at once (UIF is pending), ending the overdue phase now. */
    if (TIM17->CNT >= arr)
        TIM17->EGR = TIM_EGR_UG;
}

void TIM3_IRQHandler(void)
{
    uint32_t sr = TIM3->SR;
    TIM3->SR = ~(sr & (TIM_SR_UIF | TIM_SR_CC2IF));
    uint8_t duty = g_pb7_duty;
    if (sr & TIM_SR_UIF) {
        if (duty == 0) {
            pin_low(GPIOB, PB7_PWM_PIN);
        } else {
            pin_high(GPIOB, PB7_PWM_PIN);
            if (duty < 16u)
                TIM3->CCR2 = (uint32_t)duty * 100u;   /* 1/16 of the 1600 us period per step */
        }
    }
    if ((sr & TIM_SR_CC2IF) && duty < 16u)
        pin_low(GPIOB, PB7_PWM_PIN);
}
#endif
