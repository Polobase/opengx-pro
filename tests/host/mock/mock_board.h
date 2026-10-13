/* Host replacement for board.h, just the names the link modules use (see src/link_hw.h). */
#pragma once

#include <stdint.h>

typedef struct {
    volatile uint32_t IDR;   /* input levels (PB1 = HOST_DETECT_PIN) */
    volatile uint32_t ODR;   /* output levels driven by pin_high/pin_low */
} GPIO_TypeDef;

extern GPIO_TypeDef mock_gpiob;
#define GPIOB (&mock_gpiob)

#define HOST_DETECT_PIN 1u
#define HANDSHAKE_PIN   2u

static inline void pin_high(GPIO_TypeDef *port, uint32_t pin) { port->ODR |= 1u << pin; }
static inline void pin_low(GPIO_TypeDef *port, uint32_t pin) { port->ODR &= ~(1u << pin); }
static inline uint32_t pin_read(GPIO_TypeDef *port, uint32_t pin) { return (port->IDR >> pin) & 1u; }

extern volatile uint32_t g_ticks;
extern volatile uint8_t g_backlight_duty;
extern volatile uint8_t g_pb7_duty;

void delay_loop(uint32_t iterations);
void NVIC_SystemReset(void);
__attribute__((noreturn)) void power_off_signal(void);   /* nx tests: tests/host/nx/nx_world.c */
