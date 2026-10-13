/* Mocks of the hardware layer below the link modules. */
#include "harness.h"

#include "lcd.h"
#include "mock_board.h"
#include "uart.h"
#include "ui.h"

int g_checks, g_fails;

GPIO_TypeDef mock_gpiob;
volatile uint32_t g_ticks;
void (*uart_tx_hook)(unsigned port, uint8_t c);

char tx_log[3][1 << 14];
size_t tx_len[3];
char ev_log[1 << 14];
size_t ev_len;
static int ev_line_start[3] = { 1, 1, 1 };

uint64_t delay_total;
unsigned delay_calls, delay_calls_34155, reset_calls;
unsigned lcd_clear_calls, lcd_other_calls, ui_values_calls, ui_value_calls, ui_value_last;
int auto_ack;
static int ack_pending[2];
static const char *inject[2];

void inject_on_delay(unsigned port, const char *s)
{
    inject[port] = s;
}

volatile uint32_t g_uart_rx_bytes[UART_COUNT];
volatile uint32_t g_uart_rx_errors[UART_COUNT];
uint32_t g_uart_tx_bytes[UART_COUNT];

static void ev_put(char c)
{
    if (ev_len < sizeof ev_log - 1)
        ev_log[ev_len++] = c;
    ev_log[ev_len] = 0;
}

void tx_clear(void)
{
    memset(tx_len, 0, sizeof tx_len);
    ev_len = 0;
    ev_log[0] = 0;
    for (int i = 0; i < 3; i++)
        ev_line_start[i] = 1;
}

void mocks_reset(void)
{
    tx_clear();
    delay_total = 0;
    delay_calls = delay_calls_34155 = reset_calls = 0;
    lcd_clear_calls = lcd_other_calls = ui_values_calls = ui_value_calls = ui_value_last = 0;
    auto_ack = 0;
    uart_tx_hook = NULL;
    ack_pending[0] = ack_pending[1] = 0;
    inject[0] = inject[1] = NULL;
    mock_gpiob.IDR = 0;
    mock_gpiob.ODR = 0;
    g_backlight_duty = 8;
    g_pb7_duty = 1;
}

void check_bytes(const char *file, int line, const char *got, size_t len, const char *exp)
{
    size_t elen = strlen(exp);
    g_checks++;
    if (len == elen && memcmp(got, exp, len) == 0)
        return;
    g_fails++;
    printf("%s:%d: bytes differ (got %zu, expected %zu)\n  got: ", file, line, len, elen);
    for (size_t i = 0; i < len && i < 400; i++)
        printf(got[i] >= 32 && got[i] < 127 ? "%c" : "\\x%02X", (unsigned char)got[i]);
    printf("\n  exp: ");
    for (size_t i = 0; i < elen && i < 400; i++)
        printf(exp[i] >= 32 && exp[i] < 127 ? "%c" : "\\x%02X", (unsigned char)exp[i]);
    printf("\n");
}

void feed_bytes(unsigned port, const uint8_t *s, size_t n)
{
    while (n--)
        uart_rx_hook((enum uart_port)port, *s++);
}

void feed(unsigned port, const char *s)
{
    feed_bytes(port, (const uint8_t *)s, strlen(s));
}

/* ---- uart.h ---- */

void uart_init(void)
{
}

int uart_getc(enum uart_port port)
{
    (void)port;
    return -1;
}

void uart_putc(enum uart_port port, uint8_t c)
{
    unsigned p = (unsigned)port;
    if (uart_tx_hook)
        uart_tx_hook(p, c);
    if (tx_len[p] < sizeof tx_log[p])
        tx_log[p][tx_len[p]++] = (char)c;
    g_uart_tx_bytes[p]++;
    if (ev_line_start[p]) {
        ev_put((char)('1' + p));
        ev_put('>');
        ev_line_start[p] = 0;
    }
    ev_put((char)c);
    if (c == '\n' || p == 2) {   /* USART3 sends single bytes without CR/LF */
        if (p == 2)
            ev_put('\n');
        ev_line_start[p] = 1;
        if (p < 2 && auto_ack)
            ack_pending[p] = 1;
    }
}

void uart_puts(enum uart_port port, const char *s)
{
    while (*s)
        uart_putc(port, (uint8_t)*s++);
}

void uart_flush(enum uart_port port)
{
    (void)port;
}

/* ---- board ---- */

void delay_loop(uint32_t iterations)
{
    sim_advance_us(iterations / 16u);   /* 4 cycles per iteration at 64 MHz */
    delay_total += iterations;
    delay_calls++;
    if (iterations == 0x34155u)
        delay_calls_34155++;
    if (iterations == 0x855u) {
        ev_put('[');
        ev_put('W');
        ev_put(']');
        ev_put('\n');
    }
    for (unsigned p = 0; p < 2; p++) {   /* the generator's reply arrives while we wait */
        if (inject[p]) {
            const char *s = inject[p];
            inject[p] = NULL;
            feed(p, s);
        }
        if (ack_pending[p]) {
            ack_pending[p] = 0;
            feed(p, ":ok\r\n");
        }
    }
}

void NVIC_SystemReset(void)
{
    reset_calls++;
}
