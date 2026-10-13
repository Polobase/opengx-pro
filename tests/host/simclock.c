/* Simulated time for the host tests: microseconds since sim_reset(). The tests advance it
 * explicitly; delay_loop() advances it like the device (4 cycles per iteration at 64 MHz), and so
 * does every SPI byte while the W25Q16 model runs with microsecond timing. A tick hook runs once
 * per millisecond boundary crossed (e.g. the nx firmware's SysTick handler). */
#include "harness.h"

uint64_t sim_us;
static void (*tick_hook)(void);

void sim_reset(void)
{
    sim_us = 0;
    tick_hook = NULL;
}

void sim_on_ms(void (*fn)(void))
{
    tick_hook = fn;
}

void sim_advance_us(uint64_t us)
{
    uint64_t end = sim_us + us;
    while (tick_hook && (sim_us / 1000u + 1u) * 1000u <= end) {
        sim_us = (sim_us / 1000u + 1u) * 1000u;
        tick_hook();   /* may advance the clock itself (e.g. through delay_loop) */
    }
    if (sim_us < end)
        sim_us = end;
}

void sim_advance_ms(uint32_t ms)
{
    sim_advance_us((uint64_t)ms * 1000u);
}

uint32_t sim_ms(void)
{
    return (uint32_t)(sim_us / 1000u);
}
