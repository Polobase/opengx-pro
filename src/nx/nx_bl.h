/* Backlight (PB12) and PB7 outputs of the nx firmware (docs/nx/design/ui.md §2.12).
 *
 * PB12 has no timer output, so TIM17 runs a two-phase software PWM: its update interrupt sets
 * the pin and loads the high time, the next one clears it and loads the low time. 256 levels map
 * to the high time with a gamma of 2; levels 0 and 255 are static pin levels without interrupts.
 * Fades step the level every millisecond from the SysTick handler.
 *
 * link.c's w64 keeps its meaning through g_backlight_duty, used as a mailbox: link.c writes 8 (on
 * at the user's level) or 0 (off); the tick takes the value, writes NX_BL_W64_NONE back and holds
 * the backlight off while the host asks for that. Every w64 on lights it, as in the original.
 *
 * PB7 (load unknown) keeps the original's 16-step PWM at 625 Hz with duty g_pb7_duty / 16
 * (1 by default, factory ':b' sets 1/6/10/15): TIM3's update interrupt sets PB7, its compare
 * channel 2 clears it after g_pb7_duty * 100 us. */
#pragma once

#include <stdint.h>

#define NX_BL_LEVEL_MAX 255u
#define NX_BL_DEFAULT_LEVEL 181u   /* 181^2 / 255^2 = 50.4 %: the original's duty 8/16 */
#define NX_BL_MIN_PHASE_US 4u      /* shortest high or low phase (interrupt latency) */
#define NX_BL_W64_NONE 0xFFu       /* g_backlight_duty: no w64 pending */

extern volatile uint8_t g_backlight_duty;   /* w64 mailbox (link.c): 8 = on, 0 = off, else none */
extern volatile uint8_t g_pb7_duty;         /* written by factory.c (':b'): PB7 duty in 1/16 */

void nx_bl_init(void);                 /* device: TIM17 and TIM3; host: state reset. Output off. */
uint16_t nx_bl_on_us(uint8_t level);   /* high time of one period for a level (gamma 2) */
void nx_bl_set_user(uint8_t level);    /* the user's brightness; applied now unless w64 holds it off */
uint8_t nx_bl_user(void);
/* Moves the output level to `level` over `ms` milliseconds (0 = at once). While w64 holds the
 * backlight off the output stays off; the target is kept. */
void nx_bl_fade_to(uint8_t level, uint16_t ms);
uint8_t nx_bl_level(void);             /* output level now */
int nx_bl_host_off(void);              /* 1 while w64 holds the backlight off */
int nx_bl_w64_on_event(void);          /* 1 once after each w64 on (the UI wakes a switched-off screen) */
void nx_bl_host_release(void);         /* the goodbye: a w64 off no longer holds the light off */
void nx_bl_tick_1ms(void);             /* SysTick: w64, fades, hardware update */
