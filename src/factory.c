/* USART3 factory / calibration-jig port (docs/spec/protocols.md §2.5, §4; application.md §9.3).
 * Frames ":<a..l><decimal>\n" are parsed in the receive interrupt (USART3_4_IRQHandler
 * 0x0800D7D8) and executed in the main loop (factory_cmd_execute 0x0800522C), which answers
 * every command with the single byte 'Z'. */
#include <stdint.h>

#include "lcd.h"
#include "link.h"
#include "link_hw.h"
#include "link_internal.h"
#include "state.h"
#include "storage.h"
#include "uart.h"
#include "ui.h"

static uint8_t f_state;   /* 0x200000BE */
static uint8_t f_cmd;     /* 0x20000070 */
static uint16_t f_num;    /* 0x20000072, wraps */

void factory_reset(void)
{
    f_state = 0;
    f_cmd = 0;
    f_num = 0;
}

/* Interrupt context. After the letter, digits accumulate; any other byte except LF is skipped
 * while fewer than 8 characters followed the letter, later it drops the frame (original). */
void factory_rx(uint8_t c)
{
    if (g_factory_cmd_flags & 1u) {   /* command pending: byte dropped */
        f_state = 0;
        return;
    }
    if (f_state == 0) {
        if (c == ':') {
            f_state = 1;
            f_cmd = 0;
            f_num = 0;
        }
    } else if (f_state == 1) {
        if (c >= 'a' && c <= 'l') {
            f_cmd = c;
            f_state = 2;
        } else {
            f_state = 0;
        }
    } else if (c >= '0' && c <= '9') {
        if (f_state < 0xFFu)   /* DEVIATION: saturates; the original's u8 wrapped after 254 digits */
            f_state++;
        f_num = (uint16_t)(f_num * 10u + (c - '0'));
    } else if (c == '\n') {
        f_state = 0;
        g_factory_cmd_flags |= 1u;
    } else if (f_state <= 9u) {
        f_state++;
    } else {
        f_state = 0;
    }
}

void factory_cmd_tag(char out[8])
{
    uint16_t n = f_num;
    char digits[5];
    unsigned k = 0;
    out[0] = f_cmd ? (char)f_cmd : '-';
    do {
        digits[k++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n);
    unsigned i = 1;
    while (k)
        out[i++] = digits[--k];
    out[i] = 0;
}

static uint8_t pb7_duty_for(uint16_t n)   /* ':b' soft-PWM duty of PB7 */
{
    if (n <= 5u)
        return 1;
    if (n <= 10u)
        return 6;
    if (n <= 15u)
        return 10;
    if (n <= 20u)
        return 15;
    return 1;
}

void link_factory_execute(void)
{
    if (!(g_factory_cmd_flags & 1u))
        return;
    LINK_BARRIER();
    const uint16_t n = f_num;

    switch (f_cmd) {
    case 'a':
        if (n == 0) {   /* enter the calibration screen */
            g_cal_index = 0;
            g_ui_state = ST_CAL;
            flags_set(&g_ui_flags, UIF_REDRAW);
            lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
            ui_cal_draw_values();
        } else {        /* back to the normal UI (state 0 -> HOME or PC mode) */
            g_ui_state = ST_BOOT;
            flags_set(&g_ui_flags, UIF_REDRAW);
            lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
        }
        break;
    case 'b':
        g_cal_index = (uint8_t)n;   /* unchecked (original); indices > 28 send nothing */
        cal_apply_test_jig();
        flags_set(&g_ui_flags, UIF_REDRAW);
        g_pb7_duty = pb7_duty_for(n);
        break;
    case 'c':
        /* DEVIATION: the table is written only for a valid index (':b' can set any 0..255). */
        if (g_cal_index < 32u)
            g_cal_table[g_cal_index] = (int16_t)n;
        link_send_cal_value(g_cal_index, n);
        flags_set(&g_ui_flags, UIF_REDRAW);
        break;
    case 'd':
        if (n == 11141u) {
            cal_save(1);
            link_send_line(0, ":w61=1114111,");
        } else if (n == 11131u) {
            cal_save(0);
            link_send_line(0, ":w61=1113111,");
        }
        break;
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l': {
        unsigned idx = 21u + (unsigned)(f_cmd - 'e');
        g_cal_table[idx] = (int16_t)n;
        ui_cal_draw_value(idx);   /* drawn in any UI state, as the original */
        break;
    }
    default:
        flags_clear(&g_factory_cmd_flags, 1u);
        return;
    }
    uart_putc(UART_FACTORY, 'Z');
    uart_flush(UART_FACTORY);
    flags_clear(&g_factory_cmd_flags, 1u);
}
