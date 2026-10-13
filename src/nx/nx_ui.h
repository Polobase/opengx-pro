/* What the nx core needs from the UI (docs/nx/design/ui.md §6.2, §6.4). The UI (framework
 * nx_ui.c, components nx_comp.c, screens nx_scr_*.c) sees the core only through nx_uicore.h. */
#pragma once

#include <stdint.h>

#include "ui.h"   /* ui_cal_draw_values() / ui_cal_draw_value(): called by factory.c */

void nx_ui_init(void);                     /* after nx_lcd_init and the settings: splash, backlight */
void nx_ui_tick(uint32_t now_ms);          /* every main-loop pass: events, keys, VM diff, timers */
int nx_ui_render_step(uint32_t budget_us); /* every pass: draw pending work; 1 = more to do */
int nx_ui_busy(void);                      /* 1 while events, keys or drawing are pending */
void nx_ui_cal_screen(void);               /* the calibration boot starts (before its values) */
/* Power-off (nx_core_power_off): nx_ui_goodbye draws the goodbye screen at once; the core then
 * calls nx_ui_goodbye_step on every pass of its NX_GOODBYE_MS wait with the time left, so the
 * ring can close, the backlight fade out and the panel sleep before the PB2 signal. */
void nx_ui_goodbye(void);
void nx_ui_goodbye_step(uint32_t ms_left);
/* Core -> UI event (enum nx_event_type of nx_vm.h), from the main loop only. */
void nx_ui_post(uint8_t type, uint8_t ch, uint16_t a);
