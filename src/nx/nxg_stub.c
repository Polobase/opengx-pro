/* APP=nxgallery platform: what the engine needs besides the panel. The clock is the gallery's
 * virtual time (nxg_core.c: nx_ms); TIM6 gives the microseconds the render slices measure; the
 * emulator hooks nxg_shot to take a screenshot. No UART, no SPI flash, no watchdog. */
#include "board.h"
#include "nxg_core.h"

uint16_t nx_us16(void)
{
    return (uint16_t)TIM6->CNT;
}

void nxg_timer_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_TIM6EN;
    TIM6->CR1 = 0;
    TIM6->PSC = 63;                      /* 64 MHz / 64 = 1 MHz */
    TIM6->ARR = 0xFFFFu;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->CR1 = TIM_CR1_CEN;
}

/* The emulator's --shot-hook stops here: `name` is the second argument (r1). */
__attribute__((noinline, used)) void nxg_shot(unsigned id, const char *name)
{
    __asm volatile("" : : "r"(id), "r"(name) : "memory");
}
