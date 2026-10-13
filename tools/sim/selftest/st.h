/* Shared by the emulator self-test images (tools/sim/selftest.py): register access, the result
 * words and the start-up code. The linker script (selftest.ld) has no .data copy, so the images
 * use only zero-initialised or const globals. res[15] = DONE marks an image that ran to its end. */
#pragma once

#include <stdint.h>

#define REG(a)   (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG8(a)  (*(volatile uint8_t *)(a))
#define DONE     0xC0FFEEu

volatile uint32_t res[16];
extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);
int main(void);

void Default_Handler(void)
{
    for (;;) {
    }
}

void Reset_Handler(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; p++)
        *p = 0;
    main();
    for (;;)
        __asm volatile("wfi");
}
