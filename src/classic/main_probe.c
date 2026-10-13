/* Link probe: finds out how the PC's two USB-serial channels, the generator boards and the
 * display's USART1/2/3 are wired, and backs up the SPI flash.
 *
 * Everything it sends starts with '#', never ':' (the generator protocol), and it only acts
 * on lines that start with '#'. Commands (answered on the port they arrived on unless noted):
 *   #id                  -> #id fw=probe-1 flash=<JEDEC> lcd=<variant>
 *   #ping                -> #pong rx=Un tx=Um pb1=x pb2=y, sent on ALL three ports
 *   #pb2 0|1             -> sets PB2, answers #pb2 x rx=Un on all ports
 *   #dump AAAAAA LLLLLL  -> #d AAAAAA <32 bytes hex> ... #end AAAAAA LLLLLL <crc32>  (hex values)
 * Keys: START = send #hello on all ports, LOOP = toggle PB2, POWER held = power off,
 *       POWER+STOP held 2 s = reset. The SPI flash is only ever read. */
#include <stdint.h>

#include "board.h"
#include "fmt.h"
#include "keys.h"
#include "lcd.h"
#include "spiflash.h"
#include "uart.h"

#define SPI_FLASH_SIZE 0x200000u   /* W25Q16 */
#define TEXT_LEN 22u
#define LINE_Y(n) (32u + (n) * 26u)

struct port_state {
    char line[64];
    uint8_t len;
    char text[TEXT_LEN + 1];   /* last received characters, for the screen */
    uint8_t dirty;
};

static struct port_state port_state[UART_COUNT];

static uint8_t jedec[3];
static uint16_t lcd_variant;
static char last_cmd[32] = "-";
static uint8_t last_cmd_dirty = 1;

static const char *parse_hex(const char *s, uint32_t *out)
{
    while (*s == ' ')
        s++;
    uint32_t v = 0;
    int n = 0;
    for (;; s++, n++) {
        char c = *s;
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0')
                   : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10)
                   : (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10) : 16u;
        if (d > 15)
            break;
        v = (v << 4) | d;
    }
    *out = v;
    return n ? s : 0;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return 0;
    return *s == 0 || *s == ' ';
}

static char *port_tag(char *p, const char *key, unsigned port)
{
    p = fmt_str(p, key);
    *p++ = 'U';
    return fmt_uint(p, port + 1u);
}

static void send_line(enum uart_port port, const char *s)
{
    uart_puts(port, s);
    uart_puts(port, "\r\n");
    port_state[port].dirty = 1;   /* refresh the tx counter on screen */
}

static void cmd_ping(enum uart_port from)
{
    char buf[48], *p;
    for (unsigned tx = 0; tx < UART_COUNT; tx++) {
        p = fmt_str(buf, "#pong ");
        p = port_tag(p, "rx=", from);
        p = port_tag(p, " tx=", tx);
        p = fmt_str(p, " pb1=");
        p = fmt_uint(p, pin_read(GPIOB, HOST_DETECT_PIN));
        p = fmt_str(p, " pb2=");
        fmt_uint(p, (GPIOB->ODR >> HANDSHAKE_PIN) & 1u);
        send_line((enum uart_port)tx, buf);
    }
}

static void cmd_pb2(enum uart_port from, const char *arg)
{
    uint32_t v;
    if (!parse_hex(arg, &v) || v > 1)
        return;
    if (v)
        pin_high(GPIOB, HANDSHAKE_PIN);
    else
        pin_low(GPIOB, HANDSHAKE_PIN);
    char buf[32], *p = fmt_str(buf, "#pb2 ");
    p = fmt_uint(p, v);
    port_tag(p, " rx=", from);
    for (unsigned tx = 0; tx < UART_COUNT; tx++)
        send_line((enum uart_port)tx, buf);
}

static void cmd_id(enum uart_port from)
{
    char buf[48], *p = fmt_str(buf, "#id fw=probe-1 flash=");
    for (unsigned i = 0; i < 3; i++)
        p = fmt_hex(p, jedec[i], 2);
    p = fmt_str(p, " lcd=");
    fmt_hex(p, lcd_variant, 4);
    send_line(from, buf);
}

static void cmd_dump(enum uart_port from, const char *args)
{
    uint32_t addr, len;
    const char *rest = parse_hex(args, &addr);
    if (!rest || !parse_hex(rest, &len) || addr >= SPI_FLASH_SIZE || len == 0 || len > SPI_FLASH_SIZE - addr) {
        send_line(from, "#error dump range");
        return;
    }
    uint32_t crc = 0;
    char buf[80], *p;
    uint8_t data[32];
    for (uint32_t off = 0; off < len; off += sizeof data) {
        uint32_t n = len - off < sizeof data ? len - off : sizeof data;
        spiflash_read(addr + off, data, n);
        crc = fmt_crc32(crc, data, n);
        p = fmt_str(buf, "#d ");
        p = fmt_hex(p, addr + off, 6);
        *p++ = ' ';
        for (uint32_t i = 0; i < n; i++)
            p = fmt_hex(p, data[i], 2);
        send_line(from, buf);
    }
    p = fmt_str(buf, "#end ");
    p = fmt_hex(p, addr, 6);
    *p++ = ' ';
    p = fmt_hex(p, len, 6);
    *p++ = ' ';
    fmt_hex(p, crc, 8);
    send_line(from, buf);
}

static void handle_line(enum uart_port from, const char *line)
{
    if (line[0] != '#')
        return;
    char *p = fmt_str(last_cmd, "");
    for (unsigned i = 0; line[i] && i < 18; i++)
        *p++ = line[i];
    p = fmt_str(p, " (");
    p = port_tag(p, "", from);
    fmt_str(p, ")");
    last_cmd_dirty = 1;

    if (starts_with(line, "#id"))
        cmd_id(from);
    else if (starts_with(line, "#ping"))
        cmd_ping(from);
    else if (starts_with(line, "#pb2"))
        cmd_pb2(from, line + 4);
    else if (starts_with(line, "#dump"))
        cmd_dump(from, line + 5);
    else
        send_line(from, "#error unknown");
}

static void poll_port(enum uart_port port)
{
    struct port_state *ps = &port_state[port];
    int c;
    while ((c = uart_getc(port)) >= 0) {
        for (unsigned i = 0; i < TEXT_LEN - 1u; i++)   /* scroll the on-screen text left */
            ps->text[i] = ps->text[i + 1];
        ps->text[TEXT_LEN - 1u] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == '\n' ? '~' : '.');
        ps->text[TEXT_LEN] = 0;
        ps->dirty = 1;

        if (c == '\r' || c == '\n') {
            if (ps->len) {
                ps->line[ps->len] = 0;
                handle_line(port, ps->line);
                ps->len = 0;
            }
        } else if (ps->len < sizeof ps->line - 1u) {
            ps->line[ps->len++] = (char)c;
        }
    }
}

static void send_hello(void)
{
    char buf[40], *p;
    for (unsigned tx = 0; tx < UART_COUNT; tx++) {
        p = port_tag(fmt_str(buf, "#hello "), "tx=", tx);
        p = fmt_str(p, " pb2=");
        fmt_uint(p, (GPIOB->ODR >> HANDSHAKE_PIN) & 1u);
        send_line((enum uart_port)tx, buf);
    }
}

static void draw_port(unsigned port)
{
    char buf[48], *p = port_tag(buf, "", port);
    p = fmt_str(p, "  rx ");
    p = fmt_uint(p, g_uart_rx_bytes[port]);
    p = fmt_str(p, "  err ");
    p = fmt_uint(p, g_uart_rx_errors[port]);
    p = fmt_str(p, "  tx ");
    fmt_uint(p, g_uart_tx_bytes[port]);
    uint16_t y = (uint16_t)LINE_Y(port * 2u);
    gfx_fill(0, y, 319, y + 49u, COLOR_BLACK);
    gfx_text(8, y, buf, COLOR_WHITE, COLOR_BLACK);
    gfx_text(24, y + 26u, port_state[port].text, COLOR_YELLOW, COLOR_BLACK);
}

static void draw_pins(void)
{
    char buf[24], *p = fmt_str(buf, "PB1 ");
    p = fmt_uint(p, pin_read(GPIOB, HOST_DETECT_PIN));
    p = fmt_str(p, "  PB2 ");
    fmt_uint(p, (GPIOB->ODR >> HANDSHAKE_PIN) & 1u);
    gfx_fill(200, 2, 319, 25, COLOR_BLUE);
    gfx_text((uint16_t)(312u - gfx_text_width(buf)), 2, buf, COLOR_WHITE, COLOR_BLUE);
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
    __enable_irq();

    delay_loop(0x82355);
    lcd_variant = lcd_init();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    g_backlight_duty = 8;
    spiflash_init();
    spiflash_read_jedec_id(jedec);

    gfx_fill(0, 0, 319, 27, COLOR_BLUE);
    gfx_text(8, 2, "link probe", COLOR_WHITE, COLOR_BLUE);
    gfx_text(8, LINE_Y(7), "START hello  LOOP PB2", COLOR_GREY, COLOR_BLACK);
    for (unsigned i = 0; i < UART_COUNT; i++) {
        for (unsigned k = 0; k < TEXT_LEN; k++)   /* spaces, so the scrolling text is never cut at a NUL */
            port_state[i].text[k] = ' ';
        port_state[i].text[TEXT_LEN] = 0;
        port_state[i].dirty = 1;
    }

    uint32_t shown_pins = ~0u, last_draw = 0, held_since = 0;
    enum key_code stable = KEY_NONE, last = KEY_NONE;
    for (;;) {
        for (unsigned i = 0; i < UART_COUNT; i++)
            poll_port((enum uart_port)i);

        uint32_t now = g_ticks;
        if (now - last_draw >= 1000u) {   /* redraw at most every 100 ms */
            last_draw = now;
            for (unsigned i = 0; i < UART_COUNT; i++) {
                if (port_state[i].dirty) {
                    port_state[i].dirty = 0;
                    draw_port(i);
                }
            }
            uint32_t pins = pin_read(GPIOB, HOST_DETECT_PIN) | ((GPIOB->ODR >> HANDSHAKE_PIN) & 1u) << 1;
            if (pins != shown_pins) {
                shown_pins = pins;
                draw_pins();
            }
            if (last_cmd_dirty) {
                last_cmd_dirty = 0;
                gfx_fill(0, LINE_Y(6), 319, LINE_Y(6) + 23u, COLOR_BLACK);
                gfx_text(8, LINE_Y(6), last_cmd, COLOR_GREY, COLOR_BLACK);
            }

            enum key_code k = keys_read();
            if (k == last && k != stable) {
                stable = k;
                held_since = now;
                if (k == KEY_START)
                    send_hello();
                if (k == KEY_LOOP) {
                    if (GPIOB->ODR & (1u << HANDSHAKE_PIN))
                        pin_low(GPIOB, HANDSHAKE_PIN);
                    else
                        pin_high(GPIOB, HANDSHAKE_PIN);
                }
            }
            last = k;
            uint32_t held_ms = (now - held_since) / 10u;
            if (stable == KEY_POWER && held_ms >= 800u)
                power_off();
            if (stable == KEY_POWER_STOP && held_ms >= 2000u)
                NVIC_SystemReset();
        }
    }
}
