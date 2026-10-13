/* Engine gallery (make APP=nxgallery; tools/nx_gallery.py run --emu): renders the sheets of
 * src/nx/nxg_sheets.c with the real panel driver, band renderer and DMA on the emulated board and
 * stops in nxg_shot() after each, where `tools/emu.py --shot-hook nxg_shot:DIR` saves the glass.
 * Then it measures the engine (full frame, full-screen fill, longest band and slice) and sends one
 * "#bench ..." line on USART1 ('#' lines are ignored by the generators), and ends with the
 * power-off signal, which stops the emulator. No SPI flash, backlight fully on. Builds without
 * the vendor image. */
#include "board.h"
#include "fmt.h"
#include "lcd.h"
#include "nx_lcd.h"
#include "nx_render.h"
#include "nxg_sheets.h"
#include "uart.h"

void nxg_timer_init(void);

static uint32_t s_us;        /* microseconds, accumulated from the 16-bit TIM6 */
static uint16_t s_last;

static void tick(void)
{
    uint16_t now = (uint16_t)TIM6->CNT;
    s_us += (uint16_t)(now - s_last);
    s_last = now;
}

static void draw_bench(nx_gc *gc, void *ctx)
{
    (void)ctx;
    for (int16_t y = 8; y < 232; y = (int16_t)(y + 28)) {   /* text, shapes and bars on every band */
        nx_rrect(gc, (nx_rect){ 8, y, 312, (int16_t)(y + 24) }, 8, nx_col(gc, NX_PAL_SURFACE1));
        nx_text(gc, &nx_font_body16, 16, (int16_t)(y + 2), "Lyme Borreliosis 727.000 Hz", nx_col(gc, NX_PAL_TEXT1));
        nx_bar(gc, (nx_rect){ 250, (int16_t)(y + 9), 304, (int16_t)(y + 15) }, 30000u, nx_col(gc, NX_PAL_G1),
               nx_col(gc, NX_PAL_TRACK));
    }
}

/* One full frame: returns the wall time, adds the CPU time spent in the render slices. */
static uint32_t frame(uint32_t *cpu)
{
    nx_inval_all();
    tick();
    uint32_t t0 = s_us;
    *cpu = 0;
    for (uint32_t n = 0; n < 1000000u && (nx_render_busy() || !nx_lcd_idle()); n++) {
        tick();
        uint32_t s0 = s_us;
        nx_render_step(2000u);
        tick();
        *cpu += s_us - s0;
    }
    return s_us - t0;
}

static void bench(void)
{
    uint32_t cpu_frame, cpu_fill;
    nx_render_init();
    nx_render_layer(NX_LAYER_SCREEN, draw_bench, 0);
    nx_render_flush();
    g_nx_render_stats = (struct nx_render_stats){ 0 };
    uint32_t t_frame = frame(&cpu_frame);
    uint32_t draw_us = g_nx_render_stats.draw_us;
    nx_render_layer(NX_LAYER_SCREEN, 0, 0);
    uint32_t t_fill = frame(&cpu_fill);
    char line[160], *p = fmt_str(line, "#bench frame_us=");
    p = fmt_uint(p, t_frame);
    p = fmt_str(p, " frame_cpu_us=");
    p = fmt_uint(p, cpu_frame);
    p = fmt_str(p, " frame_draw_us=");
    p = fmt_uint(p, draw_us);
    p = fmt_str(p, " fill_us=");
    p = fmt_uint(p, t_fill);
    p = fmt_str(p, " band_us_max=");
    p = fmt_uint(p, g_nx_render_stats.band_us_max);
    p = fmt_str(p, " step_us_max=");
    p = fmt_uint(p, g_nx_render_stats.step_us_max);
    p = fmt_str(p, " dma=");
    p = fmt_uint(p, NX_LCD_DMA);
    p = fmt_str(p, " ds16=");
    p = fmt_uint(p, NX_LCD_DS16);
    fmt_str(p, "\r\n");
    uart_puts(UART_GEN_A, line);
    uart_flush(UART_GEN_A);
}

int main(void)
{
    clock_init();
    board_init();
    nxg_timer_init();
    uart_init();
    __enable_irq();

    delay_loop(0x82355);   /* as the nx firmware before lcd_init */
    lcd_init();
    nx_lcd_init();
    pin_high(GPIOB, BACKLIGHT_PIN);   /* full light: screenshots at full brightness */
    nxg_run();
    bench();
    power_off_signal();   /* done: the emulator stops at the power-off signal (a unit switches off) */
}
