/* Reset handler and STM32G070 vector table. The application is linked at 0x08004000, behind
 * the vendor bootloader (0x08000000-0x08003FFF), which jumps here. */
#include <stdint.h>

#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

__attribute__((noreturn)) void reset_c(void);
void Reset_Handler(void);

#ifdef CFG_FAULT_HOOK
/* Apps with a crash recorder (nx: -DCFG_FAULT_HOOK=nx_fault) get the stacked exception frame and
 * the exception number; the hook never returns (it resets or powers off). The handlers keep
 * their names, which the emulator watches. Everything runs on the main stack, so the frame is at
 * MSP on entry (naked: no prologue has moved it). */
#define FAULT_STR2(x) #x
#define FAULT_STR(x) FAULT_STR2(x)
void CFG_FAULT_HOOK(const uint32_t *frame, uint32_t ipsr);

__attribute__((naked)) void Default_Handler(void)
{
    __asm volatile(
        "mov r0, sp\n"
        "mrs r1, ipsr\n"
        "bl " FAULT_STR(CFG_FAULT_HOOK) "\n");
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "mov r0, sp\n"
        "mrs r1, ipsr\n"
        "bl " FAULT_STR(CFG_FAULT_HOOK) "\n");
}
#else
/* DEVIATION: anything unexpected ends in the original's power-off signal rather than a silent
 * hang (the original just loops), so the device can still be switched off. */
void Default_Handler(void)
{
    power_off_signal();
}

void HardFault_Handler(void)
{
    power_off_signal();
}
#endif

#define ALIAS __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) ALIAS;
void SVC_Handler(void) ALIAS;
void PendSV_Handler(void) ALIAS;
void SysTick_Handler(void) ALIAS;
void WWDG_IRQHandler(void) ALIAS;
void RTC_TAMP_IRQHandler(void) ALIAS;
void FLASH_IRQHandler(void) ALIAS;
void RCC_IRQHandler(void) ALIAS;
void EXTI0_1_IRQHandler(void) ALIAS;
void EXTI2_3_IRQHandler(void) ALIAS;
void EXTI4_15_IRQHandler(void) ALIAS;
void DMA1_Channel1_IRQHandler(void) ALIAS;
void DMA1_Channel2_3_IRQHandler(void) ALIAS;
void DMA1_Ch4_7_DMAMUX1_OVR_IRQHandler(void) ALIAS;
void ADC1_IRQHandler(void) ALIAS;
void TIM1_BRK_UP_TRG_COM_IRQHandler(void) ALIAS;
void TIM1_CC_IRQHandler(void) ALIAS;
void TIM3_IRQHandler(void) ALIAS;
void TIM6_IRQHandler(void) ALIAS;
void TIM7_IRQHandler(void) ALIAS;
void TIM14_IRQHandler(void) ALIAS;
void TIM15_IRQHandler(void) ALIAS;
void TIM16_IRQHandler(void) ALIAS;
void TIM17_IRQHandler(void) ALIAS;
void I2C1_IRQHandler(void) ALIAS;
void I2C2_IRQHandler(void) ALIAS;
void SPI1_IRQHandler(void) ALIAS;
void SPI2_IRQHandler(void) ALIAS;
void USART1_IRQHandler(void) ALIAS;
void USART2_IRQHandler(void) ALIAS;
void USART3_4_IRQHandler(void) ALIAS;

/* Same layout as the original (46 words, startup_stm32g070xx.s). */
__attribute__((section(".isr_vector"), used)) const void *const g_vectors[46] = {
    &_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,
    SVC_Handler,
    0, 0,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IRQHandler,                    /* IRQ 0 */
    0,
    RTC_TAMP_IRQHandler,
    FLASH_IRQHandler,
    RCC_IRQHandler,
    EXTI0_1_IRQHandler,
    EXTI2_3_IRQHandler,
    EXTI4_15_IRQHandler,
    0,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_3_IRQHandler,         /* IRQ 10 */
    DMA1_Ch4_7_DMAMUX1_OVR_IRQHandler,
    ADC1_IRQHandler,
    TIM1_BRK_UP_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    0,
    TIM3_IRQHandler,
    TIM6_IRQHandler,
    TIM7_IRQHandler,
    TIM14_IRQHandler,
    TIM15_IRQHandler,                   /* IRQ 20 */
    TIM16_IRQHandler,
    TIM17_IRQHandler,
    I2C1_IRQHandler,
    I2C2_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,
    USART2_IRQHandler,
    USART3_4_IRQHandler,                /* IRQ 29 */
};

/* Set our own stack first: don't rely on what the bootloader loaded into MSP. */
__attribute__((naked, noreturn)) void Reset_Handler(void)
{
    __asm volatile(
        "ldr r0, =_estack\n"
        "mov sp, r0\n"
        "ldr r0, =reset_c\n"
        "bx r0\n");
}

void reset_c(void)
{
    /* Whatever the bootloader enabled must not fire into our vector table half-initialised. */
    __disable_irq();
    SysTick->CTRL = 0;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    SCB->VTOR = (uint32_t)g_vectors;
    __DSB();
    __ISB();

    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    main();
    power_off_signal();
}
