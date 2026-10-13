/* GX Pro display board: pins, clock, time base and power control.
 * Pin map and timings follow docs/spec/hardware.md (sections 2-4). */
#pragma once

#include <stdint.h>
#include "stm32g0xx.h"

/* LCD (ST7789 on SPI2: PB13 SCK, PB15 MOSI) */
#define LCD_CS_PORT     GPIOA
#define LCD_CS_PIN      8u
#define LCD_DC_PORT     GPIOC
#define LCD_DC_PIN      6u
#define LCD_RST_PORT    GPIOC
#define LCD_RST_PIN     7u

/* External SPI NOR flash (SPI1: PA5 SCK, PA6 MISO, PA7 MOSI) */
#define FLASH_CS_PORT   GPIOA
#define FLASH_CS_PIN    1u

/* Outputs driven by the TIM3 soft PWM */
#define BACKLIGHT_PIN   12u     /* PB12 */
#define PB7_PWM_PIN     7u      /* PB7, load unknown */

/* Control lines to the rest of the device (roles inferred, see hardware spec §8) */
#define HOST_DETECT_PIN 1u      /* PB1 input: 1 = PC mode */
#define HANDSHAKE_PIN   2u      /* PB2 output: power-off signal / bus ownership */

static inline void pin_high(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = 1u << pin; }
static inline void pin_low(GPIO_TypeDef *port, uint32_t pin) { port->BRR = 1u << pin; }
static inline uint32_t pin_read(GPIO_TypeDef *port, uint32_t pin) { return (port->IDR >> pin) & 1u; }

/* Classic time base (board_tim3.c): 100 us ticks from the TIM3 interrupt (10 kHz, as the original). */
extern volatile uint32_t g_ticks;
/* The original's u8 tick counter (0x20000008): +1 per TIM3 tick, wraps every 25.6 ms. */
extern volatile uint8_t g_tick8;
/* Called at the end of every TIM3 tick; the application overrides it (default: empty). */
void tick_isr_hook(void);
/* Soft-PWM duties, 0..16 (16 = always on). Backlight 8 = the original's normal level. */
extern volatile uint8_t g_backlight_duty;
extern volatile uint8_t g_pb7_duty;

/* Returns 1 when the 8 MHz crystal runs, 0 when it fell back to the HSI (both give 64 MHz). */
int clock_init(void);
void board_init(void);
/* Called at the end of board_init(): starts the time base and soft PWM. The classic apps link
 * board_tim3.c; the default (weak) does nothing. */
void board_timebase_init(void);

/* The original's busy-wait loop, instruction for instruction, so its delay constants keep
 * their real-world length. Measured cost per iteration: see bringup screen. */
void delay_loop(uint32_t iterations);
void delay_ms(uint32_t ms);

/* Original power-off signal: PB2 square wave (half period delay_loop(213)) forever. */
__attribute__((noreturn)) void power_off_signal(void);
