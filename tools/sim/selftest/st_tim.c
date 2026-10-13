/* Timers (tools/sim/selftest.py "tim"): compare flags and interrupts, OCxPE preload, a compare
 * value written without preload, CCxG, and the interrupts of TIM3 (16), TIM6 (17), TIM14 (19),
 * TIM16 (21) and TIM17 (22). All counters run at 1 MHz; the test lasts ten TIM6 periods (10 ms). */
#include "st.h"

#define RCC_APBENR1 0x4002103Cu
#define RCC_APBENR2 0x40021040u
#define TIM3        0x40000400u
#define TIM6        0x40001000u
#define TIM14       0x40002000u
#define TIM16       0x40014400u
#define TIM17       0x40014800u
#define CR1         0x00u
#define DIER        0x0Cu
#define SR          0x10u
#define EGR         0x14u
#define CCMR1       0x18u
#define CNT         0x24u
#define PSC         0x28u
#define ARR         0x2Cu
#define CCR1        0x34u
#define CCR2        0x38u
#define CCR3        0x3Cu
#define NVIC_ISER   0xE000E100u

static volatile uint32_t periods, cc1_n, cc1_cnt, cc2_n, cc2_cnt[8], cc3_n, cc3_cnt;
static volatile uint32_t t6_n, t14_n, t16_n, t17_up, t17_cc;

void TIM3_IRQHandler(void)
{
    uint32_t sr = REG(TIM3 + SR) & REG(TIM3 + DIER);
    REG(TIM3 + SR) = ~sr;
    if (sr & 1u)
        periods++;
    if (sr & 2u) {                              /* CC1 at 250 */
        cc1_n++;
        cc1_cnt = REG(TIM3 + CNT);
        if (periods == 3u)
            REG(TIM3 + CCR2) = 800u;            /* OC2PE: takes effect at the next update */
        if (periods == 6u) {
            REG(TIM3 + CCR3) = 300u;            /* no preload: acts in this period */
            REG(TIM3 + SR) = ~8u;               /* CC3IF is already set by the match at 0 */
            REG(TIM3 + DIER) |= 1u << 3;
        }
    }
    if (sr & 4u) {                              /* CC2 */
        if (cc2_n < 8u)
            cc2_cnt[cc2_n] = REG(TIM3 + CNT);
        cc2_n++;
    }
    if (sr & 8u) {                              /* CC3, once */
        cc3_n++;
        cc3_cnt = REG(TIM3 + CNT);
        REG(TIM3 + DIER) &= ~(1u << 3);
    }
}

void TIM6_IRQHandler(void)
{
    REG(TIM6 + SR) = ~1u;
    t6_n++;
}

void TIM14_IRQHandler(void)
{
    REG(TIM14 + SR) = ~2u;
    t14_n++;
}

void TIM16_IRQHandler(void)
{
    REG(TIM16 + SR) = ~2u;
    t16_n++;
}

void TIM17_IRQHandler(void)
{
    uint32_t sr = REG(TIM17 + SR);
    REG(TIM17 + SR) = ~sr;
    if (sr & 1u)
        t17_up++;
    if (sr & 2u)
        t17_cc++;
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [16 + 16] = TIM3_IRQHandler, [16 + 17] = TIM6_IRQHandler, [16 + 19] = TIM14_IRQHandler,
    [16 + 21] = TIM16_IRQHandler, [16 + 22] = TIM17_IRQHandler,
};

static void start(uint32_t t, uint32_t arr, uint32_t dier)
{
    REG(t + PSC) = 63u;                         /* 1 MHz */
    REG(t + ARR) = arr;
    REG(t + EGR) = 1u;                          /* UG: load PSC and the preloaded CCRx */
    REG(t + SR) = 0;
    REG(t + DIER) = dier;
    REG(t + CR1) = 1u;
}

int main(void)
{
    REG(RCC_APBENR1) |= (1u << 1) | (1u << 4);                  /* TIM3, TIM6 */
    REG(RCC_APBENR2) |= (1u << 15) | (1u << 17) | (1u << 18);   /* TIM14, TIM16, TIM17 */
    REG(NVIC_ISER) = (1u << 16) | (1u << 17) | (1u << 19) | (1u << 21) | (1u << 22);
    REG(TIM3 + CCMR1) = 1u << 11;              /* OC2PE */
    REG(TIM3 + CCR1) = 250u;
    REG(TIM3 + CCR2) = 500u;
    REG(TIM17 + CCR1) = 40u;
    REG(TIM14 + CCR1) = 20u;
    REG(TIM16 + CCR1) = 20u;
    start(TIM3, 999u, 1u | 2u | 4u);            /* 1 ms: UIE, CC1IE, CC2IE */
    start(TIM17, 99u, 1u | 2u);                 /* 100 us: UIE, CC1IE */
    start(TIM14, 199u, 2u);                     /* 200 us: CC1IE */
    start(TIM16, 199u, 2u);
    start(TIM6, 999u, 1u);                      /* 1 ms: the test lasts ten of these */
    while (t6_n < 10u)
        __asm volatile("wfi");
    REG(TIM3 + CR1) = 0;
    REG(TIM17 + CR1) = 0;
    REG(TIM14 + CR1) = 0;
    REG(TIM16 + CR1) = 0;
    REG(TIM6 + CR1) = 0;
    res[0] = cc1_n;
    res[1] = cc2_n;
    res[2] = cc1_cnt;
    res[3] = cc2_cnt[3];
    res[4] = cc2_cnt[4];
    res[5] = cc3_n;
    res[6] = cc3_cnt;
    res[8] = t17_up;
    res[9] = t17_cc;
    res[10] = t14_n;
    res[11] = t16_n;
    res[12] = periods;
    REG(TIM14 + SR) = 0;
    REG(TIM14 + EGR) = 2u;                      /* CC1G: software compare event */
    res[13] = REG(TIM14 + SR) & 2u;
    res[15] = DONE;
    return 0;
}
