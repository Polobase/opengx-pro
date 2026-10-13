/* Bring-up firmware: proves build -> encode -> flash -> boot on the real device and measures
 * a few things the reverse engineering could not settle. It never transmits on any UART and
 * never writes flash (internal or SPI).
 *
 * Keys: POWER held 0.8 s = original power-off sequence; POWER+STOP held 2 s = MCU reset. */
#include <stdint.h>

#include "assets.h"
#include "board.h"
#include "keys.h"
#include "lcd.h"
#include "spiflash.h"
#include "uart.h"
#include "fmt.h"

#define LINE_H 26u
#define LINE_Y(n) (32u + (n) * LINE_H)
#define VALUE_X 150u

static void show_line(unsigned line, const char *label, const char *value)
{
    uint16_t y = (uint16_t)LINE_Y(line);
    gfx_fill(0, y, 319, y + 23u, COLOR_BLACK);
    gfx_text(8, y, label, COLOR_GREY, COLOR_BLACK);
    gfx_text(VALUE_X, y, value, COLOR_WHITE, COLOR_BLACK);
}

/* The original boot splash, blit for blit (ui_draw_boot_splash 0x080054F4). */
static void draw_boot_splash(void)
{
    lcd_blit1(0x1E, 0x8D, 0x78, 0x13F, asset_bmp_boot_ghost_logo, 0x460, COLOR_WHITE, COLOR_BLACK);
    lcd_blit1(0x1E, 0x35, 0x83, 0x13F, asset_bmp_red2_small, 0x2A, COLOR_RED, COLOR_BLACK);
    lcd_blit1(0xAD, 0xC4, 0x32, 0x13F, asset_bmp_boot_welcome, 0x27C, COLOR_WHITE, COLOR_BLACK);
    lcd_blit1(0xAE, 0xC5, 0x1E, 0x13F, asset_bmp_red2_large, 0x42, COLOR_RED, COLOR_BLACK);
    lcd_blit1(200, 0xD7, 0x3C, 0x13F, asset_bmp_boot_tagline, 0x16C, COLOR_WHITE, COLOR_BLACK);
}

/* CPU cycles per delay_loop iteration x100, measured with SysTick (the original assumed 3). */
static uint32_t measure_delay_loop(void)
{
    SysTick->LOAD = 0xFFFFFFu;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    __disable_irq();
    uint32_t t0 = SysTick->VAL;
    delay_loop(100000);
    uint32_t t1 = SysTick->VAL;
    __enable_irq();
    SysTick->CTRL = 0;
    return ((t0 - t1) & 0xFFFFFFu) / 1000u;
}

static void power_off(void)
{
    g_backlight_duty = 0;
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    const char *msg = "Goodbye";
    gfx_text((uint16_t)((320u - gfx_text_width(msg)) / 2u), 108, msg, COLOR_WHITE, COLOR_BLACK);
    g_backlight_duty = 8;
    delay_loop(64000000u);   /* the original's ~4 s before the signal */
    power_off_signal();
}

int main(void)
{
    int hse = clock_init();
    board_init();
    uart_init();   /* receive only: this app never transmits */
    __enable_irq();

    delay_loop(0x82355);
    uint16_t variant = lcd_init();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    g_backlight_duty = 8;
    draw_boot_splash();

    uint32_t loop_x100 = measure_delay_loop();
    spiflash_init();
    uint8_t id[3], names[8];
    spiflash_read_jedec_id(id);
    spiflash_read(0x5000, names, sizeof names);
    delay_ms(2000);

    char buf[40], *p;
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    gfx_fill(0, 0, 319, 27, COLOR_BLUE);
    gfx_text(8, 2, "OpenGX Pro bringup", COLOR_WHITE, COLOR_BLUE);

    p = fmt_str(buf, "variant ");
    *p++ = lcd_variant_name(variant);
    p = fmt_str(p, " (");
    p = fmt_hex(p, variant, 4);
    fmt_str(p, ")");
    show_line(0, "LCD", buf);

    show_line(1, "Clock", hse ? "HSE 64 MHz" : "HSI 64 MHz (no HSE!)");

    p = fmt_uint(buf, loop_x100 / 100u);
    *p++ = '.';
    p = fmt_uint(p, (loop_x100 % 100u) / 10u);
    p = fmt_uint(p, loop_x100 % 10u);
    fmt_str(p, " cycles");
    show_line(2, "Delay loop", buf);

    p = buf;
    for (unsigned i = 0; i < 3; i++) {
        p = fmt_hex(p, id[i], 2);
        *p++ = ' ';
    }
    *--p = 0;
    show_line(3, "Flash ID", buf);

    p = buf;
    for (unsigned i = 0; i < 6; i++) {
        p = fmt_hex(p, names[i], 2);
        *p++ = ' ';
    }
    *--p = 0;
    show_line(4, "Flash 5000", buf);

    gfx_text(8, LINE_Y(7), "Hold POWER to switch off", COLOR_GREY, COLOR_BLACK);

    enum key_code shown_key = (enum key_code)-1, stable = KEY_NONE, last = KEY_NONE;
    uint32_t shown_pb1 = 2, shown_secs = ~0u, held_since = 0;
    for (;;) {
        enum key_code k = keys_read();
        if (k == last && k != stable) {   /* two equal samples 10 ms apart */
            stable = k;
            held_since = g_ticks;
        }
        last = k;

        if (stable != shown_key) {
            shown_key = stable;
            p = fmt_uint(buf, stable);
            p = fmt_str(p, "  ");
            fmt_str(p, key_name(stable));
            show_line(5, "Key", buf);
        }
        uint32_t pb1 = pin_read(GPIOB, HOST_DETECT_PIN);
        if (pb1 != shown_pb1) {
            shown_pb1 = pb1;
            show_line(6, "PB1 (PC)", pb1 ? "1  high" : "0  low");
        }
        uint32_t secs = g_ticks / 10000u;
        if (secs != shown_secs) {   /* uptime, right-aligned in the title bar */
            shown_secs = secs;
            fmt_str(fmt_uint(buf, secs), " s");
            gfx_fill(250, 2, 319, 25, COLOR_BLUE);
            gfx_text((uint16_t)(312u - gfx_text_width(buf)), 2, buf, COLOR_WHITE, COLOR_BLUE);
        }

        uint32_t held_ms = (g_ticks - held_since) / 10u;
        if (stable == KEY_POWER && held_ms >= 800u)
            power_off();
        if (stable == KEY_POWER_STOP && held_ms >= 2000u)
            NVIC_SystemReset();

        delay_ms(10);
    }
}
