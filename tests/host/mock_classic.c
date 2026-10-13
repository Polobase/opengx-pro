/* What the classic application provides to the link modules, for test_link only: the soft-PWM
 * duties (board_tim3.c), the calibration-screen hooks of the classic UI (ui.c) and the polled
 * screen clear (lcd_draw.c). test_nx links the nx firmware's own versions instead
 * (src/nx/nx_bl.c, src/nx/nx_ui_stub.c, src/nx/nx_render.c). */
#include "harness.h"

#include "lcd.h"
#include "mock_board.h"
#include "ui.h"

volatile uint8_t g_backlight_duty;
volatile uint8_t g_pb7_duty;

void ui_cal_draw_values(void)
{
    ui_values_calls++;
}

void ui_cal_draw_value(unsigned idx)
{
    ui_value_calls++;
    ui_value_last = idx;
}

void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (x0 == 0 && y0 == 0 && x1 == 239 && y1 == 319 && color == 0)
        lcd_clear_calls++;
    else
        lcd_other_calls++;
}
