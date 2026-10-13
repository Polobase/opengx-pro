/* Link test app (make APP=linktest): the firmware's serial protocols and SPI-flash storage
 * (link.c, factory.c, calib.c, storage.c) on the real device, without UI screens and without
 * the program runner, so they can be exercised from the Mac:
 *
 *   USART1 / USART2  host lines :g :p :n :w, handled exactly as in the firmware (replies,
 *                    uploads stored in the SPI flash, PC-mode mirrors of w11/w14/w15/w24/w25)
 *   USART3           factory commands :a..:l, reply 'Z'
 *
 * Keys: START = send ":w11=0,0," (outputs off) to G1 and G2 with the original's PB2 sequence
 *       and show whether each generator answered :ok; LOOP = toggle PB2 (0 = PC connected);
 *       POWER held 0.8 s = power off; POWER+STOP held 2 s = reset.
 *
 * Nothing is sent on the generator links except replies to host lines, the w92 answer, the
 * START test and what USART3 factory commands send. The SPI flash is written only by host
 * uploads, as in the firmware. */
#include <stdint.h>

#include "board.h"
#include "fmt.h"
#include "keys.h"
#include "lcd.h"
#include "link.h"
#include "link_internal.h"
#include "spiflash.h"
#include "state.h"
#include "storage.h"
#include "uart.h"
#include "ui.h"

#define ROWS 8u
#define ROW_Y(n) (32u + (n) * 26u)
#define TEXT_LEN 40u

static char shown[ROWS][TEXT_LEN];
static uint32_t frames[3];
static char last_tag[3][8] = { "-", "-", "-" };
static char gen_result[2][16] = { "-", "-" };
static unsigned cal_screen_draws, cal_value_last = 99;

/* ui.h functions called by the factory commands; the real ones belong to the UI module. */
void ui_cal_draw_values(void)
{
    cal_screen_draws++;
}

void ui_cal_draw_value(unsigned idx)
{
    cal_value_last = idx;
}

static char *fmt_u64(char *p, uint64_t v)
{
    char tmp[20];
    unsigned n = 0;
    do {
        tmp[n++] = (char)('0' + (unsigned)(v % 10u));
        v /= 10u;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    *p = 0;
    return p;
}

static void row(unsigned n, const char *text)
{
    const char *a = text;
    char *b = shown[n];
    while (*a && *a == *b) {
        a++;
        b++;
    }
    if (*a == *b)
        return;   /* unchanged */
    unsigned i = 0;
    for (; text[i] && i < TEXT_LEN - 1u; i++)
        shown[n][i] = text[i];
    shown[n][i] = 0;
    uint16_t y = (uint16_t)ROW_Y(n);
    gfx_fill(0, y, 319, y + 23u, COLOR_BLACK);
    gfx_text(8, y, shown[n], COLOR_WHITE, COLOR_BLACK);
}

static void draw_frame(void)
{
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    gfx_fill(0, 0, 319, 27, COLOR_BLUE);
    gfx_text(8, 2, "OpenGX Pro linktest", COLOR_WHITE, COLOR_BLUE);
    for (unsigned n = 0; n < ROWS; n++)
        shown[n][0] = 0x7F;   /* force a redraw */
}

static void update_screen(void)
{
    char buf[TEXT_LEN], *p;
    for (unsigned ch = 0; ch < 2; ch++) {
        p = fmt_str(buf, ch ? "G2 " : "G1 ");
        p = fmt_uint(p, frames[ch]);
        p = fmt_str(p, " lines, last ");
        p = fmt_str(p, last_tag[ch]);
        p = fmt_str(p, "  :ok ");
        fmt_str(p, gen_result[ch]);
        row(ch, buf);

        const struct channel *c = &g_ch[ch];
        p = fmt_str(buf, ch ? "PC G2 " : "PC G1 ");
        p = fmt_u64(p, c->pc_freq[0]);
        p = fmt_str(p, " / ");
        p = fmt_u64(p, c->pc_freq[1]);
        p = fmt_str(p, (g_misc_flags & MISC_OUT_EN(ch, 0)) ? " on" : " off");
        p = fmt_str(p, (g_misc_flags & MISC_OUT_EN(ch, 1)) ? "/on" : "/off");
        fmt_str(p, (g_ui_flags & UIF_SYNC(ch)) ? " sync" : "");
        row(2u + ch, buf);
    }
    p = fmt_str(buf, "U3 ");
    p = fmt_uint(p, frames[2]);
    p = fmt_str(p, " cmds, last ");
    p = fmt_str(p, last_tag[2]);
    p = fmt_str(p, "  cal ");
    p = fmt_uint(p, g_cal_index);
    p = fmt_str(p, g_ui_state == ST_CAL ? " CAL" : "");
    row(4, buf);

    p = fmt_str(buf, "PB1 ");
    p = fmt_uint(p, pin_read(GPIOB, HOST_DETECT_PIN));
    p = fmt_str(p, "  PB2 ");
    p = fmt_uint(p, (GPIOB->ODR >> HANDSHAKE_PIN) & 1u);
    p = fmt_str(p, "  BL ");
    p = fmt_uint(p, g_backlight_duty);
    p = fmt_str(p, "  PB7 ");
    fmt_uint(p, g_pb7_duty);
    row(5, buf);

    p = fmt_str(buf, "cal0 ");
    p = fmt_uint(p, (uint16_t)g_cal_table[0]);
    p = fmt_str(p, "  name0 ");
    unsigned i = 0;
    for (; i < 12u && g_names[0][i]; i++)
        p[i] = g_names[0][i];
    p[i] = 0;
    row(6, buf);

    p = fmt_str(buf, "rx ");
    p = fmt_uint(p, g_uart_rx_bytes[0]);
    *p++ = '/';
    p = fmt_uint(p, g_uart_rx_bytes[1]);
    *p++ = '/';
    p = fmt_uint(p, g_uart_rx_bytes[2]);
    p = fmt_str(p, " err ");
    fmt_uint(p, g_uart_rx_errors[0] + g_uart_rx_errors[1] + g_uart_rx_errors[2]);
    row(7, buf);
}

/* The original's PC-mode entry sequence: PB2 = 1, 10 ms, command + :ok wait per link, 10 ms,
 * PB2 = 0. The wait takes ~40 ms without an answer. */
static void generator_test(void)
{
    pin_high(GPIOB, HANDSHAKE_PIN);
    delay_loop(0x34155);
    for (unsigned ch = 0; ch < 2; ch++) {
        uint32_t t0 = g_ticks;
        link_send_line(ch, ":w11=0,0,");
        link_wait_ok();
        uint32_t t = g_ticks - t0;   /* 100 us units, including the ~1 ms transmit */
        char *p = gen_result[ch];
        if (t >= 380u) {
            fmt_str(p, "timeout");
        } else {
            p = fmt_uint(p, t / 10u);
            *p++ = '.';
            p = fmt_uint(p, t % 10u);
            fmt_str(p, " ms");
        }
    }
    delay_loop(0x34155);
    pin_low(GPIOB, HANDSHAKE_PIN);
}

static void power_off(void)
{
    g_backlight_duty = 0;
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    gfx_text(120, 108, "Goodbye", COLOR_WHITE, COLOR_BLACK);
    g_backlight_duty = 8;
    delay_loop(64000000u);
    power_off_signal();
}

int main(void)
{
    clock_init();
    board_init();
    uart_init();
    link_init();
    spiflash_init();
    storage_init();
    __enable_irq();

    delay_loop(0x82355);
    lcd_init();
    names_load();
    for (unsigned ch = 0; ch < 2; ch++) {   /* the original's power-on program defaults */
        g_ch[ch].prog.hdr[HDR_OFFS] = 120;
        g_ch[ch].prog.hdr[HDR_DWELL] = 1;
        g_ch[ch].prog.hdr[HDR_COUNT] = 1;
    }
    cal_load();
    draw_frame();
    g_backlight_duty = 8;

    enum key_code stable = KEY_NONE, last = KEY_NONE;
    uint32_t held_since = 0, last_scan = 0;
    for (;;) {
        for (unsigned ch = 0; ch < 2; ch++) {
            if (g_ui_flags & UIF_LINE(ch)) {
                link_frame_tag(ch, last_tag[ch]);
                link_process(ch);
                frames[ch]++;
            }
        }
        if (g_factory_cmd_flags & 1u) {
            factory_cmd_tag(last_tag[2]);
            link_factory_execute();
            frames[2]++;
        }
        if (g_ui_flags & UIF_REDRAW) {   /* e.g. after a factory :a cleared the screen */
            flags_clear(&g_ui_flags, UIF_REDRAW);
            draw_frame();
        }

        if (g_ticks - last_scan >= 100u) {   /* keys every 10 ms, two equal samples */
            last_scan = g_ticks;
            enum key_code k = keys_read();
            if (k == last && k != stable) {
                stable = k;
                held_since = g_ticks;
                if (k == KEY_START)
                    generator_test();
                else if (k == KEY_LOOP)
                    GPIOB->ODR ^= 1u << HANDSHAKE_PIN;
            }
            last = k;
            uint32_t held_ms = (g_ticks - held_since) / 10u;
            if (stable == KEY_POWER && held_ms >= 800u)
                power_off();
            if (stable == KEY_POWER_STOP && held_ms >= 2000u)
                NVIC_SystemReset();
            update_screen();
        }
    }
}
