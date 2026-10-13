/* Screens of the original UI, pixel-identical (docs/spec/display.md §6-9, application.md §5.2).
 * Everything reads the shared state in state.h; nothing here changes application state except
 * the render bookkeeping the original also does in ui_render (UIF_REDRAW, g_ui_prev_state,
 * g_marquee_prev_offset, g_pc_flags "up to date" bits, UIF_MARQUEE, g_marquee_offset reset on a
 * full render) and the name bitmap (g_name_bmp, g_name_bmp_width). */
#pragma once

void ui_draw_boot_splash(void);        /* 0x080054F4 */
void ui_draw_shutdown_screen(void);    /* 0x08005ED0 */
void ui_render(void);                  /* 0x080065D8 incl. ui_render_incremental 0x0800773E */
void ui_cal_draw_values(void);         /* 0x0800DED4: the 29-value calibration grid */
void ui_cal_draw_value(unsigned idx);  /* one value at its grid position (USART3 'e'..'l' path) */
void ui_name_render_bitmap(unsigned slot);   /* 0x0800DC90: g_names[slot] -> g_name_bmp */
