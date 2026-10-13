/* Display and board bring-up for the nx firmware (docs/nx/drop0-checklist.md). Answers on the
 * real panel the questions the nx display driver depends on:
 *   page 1  orientation: which MADCTL value gives upright landscape addressing (0x60 expected)
 *   page 2  bus speed: full-screen fill via the classic 8-bit path, 16-bit frames, 16-bit DMA
 *   page 3  hardware scroll: direction and artefacts of VSCSAD panning in landscape
 *   page 4  sleep: SLPIN/SLPOUT and DISPOFF/DISPON keep the picture
 *   page 5  backlight: TIM17 two-phase PWM on PB12, 256 levels, 625 Hz / 1 kHz / 2 kHz
 *   page 6  sensors and keys: MCU temperature, VDD, raw key bits (incl. PB6/PB10), PB1 live
 * Keys: LOOP next page (on page 6: hold 2 s), START run the page's test, UP/DOWN the page's
 * value, G1/G2 the page's option, POWER held 0.8 s power off. Neither the generator links nor
 * the SPI flash are used.
 * Labels are drawn through the classic path (8-bit SPI, MADCTL 0, vendor font), so they are the
 * upright reference for the test patterns drawn in the nx mode (16-bit frames, landscape MADCTL). */
#include <stdint.h>

#include "board.h"
#include "fmt.h"
#include "lcd.h"
#include "lcd_bus.h"

#define COLOR_GREEN  0x07E0u
#define COLOR_PBLUE  0x001Fu
#define COLOR_CYAN   0x07FFu
#define DMAREQ_SPI2_TX 19u                                   /* RM0454 DMAMUX request table */
#define TS_CAL1  (*(const volatile uint16_t *)0x1FFF75A8u)   /* sensor at 30 °C, VDDA 3.0 V */
#define VREF_CAL (*(const volatile uint16_t *)0x1FFF75AAu)   /* VREFINT at 30 °C, VDDA 3.0 V */
#define ADC_CH_TEMP 12u
#define ADC_CH_VREF 13u
#define WAIT_LIMIT  2000000u                                 /* bound for every flag wait */

/* ---- time: SysTick 1 kHz, TIM6 in 10 us counts ---- */

static volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    g_ms++;
}

static void timers_init(void)
{
    SysTick_Config(SystemCoreClock / 1000u);
    RCC->APBENR1 |= RCC_APBENR1_TIM6EN;
    TIM6->PSC = 639;                 /* 64 MHz / 640: 10 us per count, wraps after 655 ms */
    TIM6->ARR = 0xFFFFu;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->CR1 = TIM_CR1_CEN;
}

static uint16_t t10us(void)
{
    return (uint16_t)TIM6->CNT;
}

static void wait_ms(uint32_t ms)
{
    uint32_t t = g_ms;
    while (g_ms - t < ms) {
    }
}

static int wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t n = 0; n < WAIT_LIMIT; n++)
        if ((*reg & mask) == want)
            return 1;
    return 0;
}

/* ---- backlight: TIM17 update interrupt toggles PB12 (PB12 has no timer output) ---- */

static volatile uint16_t bl_on_us, bl_off_us;
static volatile uint8_t bl_high;
static uint8_t bl_level = 128u;
static uint16_t bl_period_us = 1000u;

void TIM17_IRQHandler(void)
{
    TIM17->SR = ~TIM_SR_UIF;
    if (bl_high) {
        pin_low(GPIOB, BACKLIGHT_PIN);
        bl_high = 0;
        TIM17->ARR = bl_off_us - 1u;
    } else {
        pin_high(GPIOB, BACKLIGHT_PIN);
        bl_high = 1;
        TIM17->ARR = bl_on_us - 1u;
    }
}

/* Level 0..255 on a square-law curve; 0 and 255 are static levels without interrupts. */
static void backlight_set(uint8_t level, uint16_t period_us)
{
    bl_level = level;
    bl_period_us = period_us;
    TIM17->CR1 = 0;
    TIM17->DIER = 0;
    NVIC_DisableIRQ(TIM17_IRQn);
    if (level == 0 || level == 255u) {
        if (level)
            pin_high(GPIOB, BACKLIGHT_PIN);
        else
            pin_low(GPIOB, BACKLIGHT_PIN);
        return;
    }
    uint32_t on = (uint32_t)period_us * level * level / 65025u;
    if (on < 2u)
        on = 2u;
    if (on > period_us - 2u)
        on = period_us - 2u;
    bl_on_us = (uint16_t)on;
    bl_off_us = (uint16_t)(period_us - on);
    bl_high = 0;
    pin_low(GPIOB, BACKLIGHT_PIN);
    TIM17->PSC = 63;                 /* 1 MHz */
    TIM17->ARR = bl_off_us - 1u;
    TIM17->EGR = TIM_EGR_UG;
    TIM17->SR = 0;
    TIM17->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM17_IRQn, 1);
    NVIC_EnableIRQ(TIM17_IRQn);
    TIM17->CR1 = TIM_CR1_CEN;
}

/* ---- panel: classic mode (8-bit, MADCTL 0) and nx mode (16-bit frames, landscape) ---- */

static int g_nx;
static uint8_t g_madctl;

static void spi_drain(void)
{
    wait_flag(&SPI2->SR, SPI_SR_FTLVL, 0);
    lcd_bus_end();
}

static void spi_frames(unsigned bits)   /* 8 or 16, between transfers only */
{
    spi_drain();
    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR2 = (uint32_t)(bits - 1u) << SPI_CR2_DS_Pos;
    SPI2->CR1 |= SPI_CR1_SPE;
}

static void cmd8(uint8_t cmd, uint8_t param)   /* one command with one parameter, 8-bit mode */
{
    lcd_bus_begin(0);
    lcd_bus_put(cmd);
    lcd_bus_end();
    lcd_bus_begin(1);
    lcd_bus_put(param);
    lcd_bus_end();
}

static inline void put16(uint16_t v)
{
    for (uint32_t n = 0; !(SPI2->SR & SPI_SR_TXE) && n < WAIT_LIMIT; n++) {
    }
    *(volatile uint16_t *)&SPI2->DR = v;
}

/* 16-bit mode: the command goes as frame 0x00cc (0x00 = NOP), parameters as 16-bit frames. */
static void cmd16(uint8_t cmd, const uint16_t *param, unsigned n)
{
    lcd_bus_begin(0);
    put16(cmd);
    spi_drain();
    if (n) {
        lcd_bus_begin(1);
        while (n--)
            put16(*param++);
        spi_drain();
    }
}

static void nx_mode(uint8_t madctl)
{
    if (g_nx && madctl == g_madctl)
        return;
    if (g_nx)
        spi_frames(8);
    cmd8(0x36, madctl);   /* MADCTL has one parameter byte: send it in 8-bit mode */
    spi_frames(16);
    g_nx = 1;
    g_madctl = madctl;
}

static void classic_mode(void)
{
    if (!g_nx)
        return;
    spi_frames(8);
    cmd8(0x36, 0x00);
    g_nx = 0;
}

static void window16(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t c[2] = { x0, x1 }, r[2] = { y0, y1 };
    cmd16(0x2A, c, 2);
    cmd16(0x2B, r, 2);
    cmd16(0x2C, 0, 0);
}

static void fill16(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t n = (uint32_t)(x1 - x0 + 1u) * (uint32_t)(y1 - y0 + 1u);
    window16(x0, y0, x1, y1);
    lcd_bus_begin(1);
    while (n--)
        put16(color);
    spi_drain();
}

/* ---- DMA1 channel 1 <- DMAMUX channel 0 <- SPI2_TX ---- */

static void dma_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMAMUX1_Channel0->CCR = DMAREQ_SPI2_TX;
    DMA1_Channel1->CPAR = (uint32_t)&SPI2->DR;
}

/* count 16-bit frames from src (incrementing if minc); 0 on timeout (e.g. not modelled) */
static int dma_send(const uint16_t *src, uint32_t count, int minc)
{
    while (count) {
        uint32_t n = count > 65535u ? 65535u : count;
        DMA1_Channel1->CCR = 0;
        DMA1->IFCR = DMA_IFCR_CGIF1;
        DMA1_Channel1->CMAR = (uint32_t)src;
        DMA1_Channel1->CNDTR = n;
        DMA1_Channel1->CCR = DMA_CCR_DIR | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0 | (minc ? DMA_CCR_MINC : 0u) | DMA_CCR_EN;
        SPI2->CR2 |= SPI_CR2_TXDMAEN;
        int ok = wait_flag(&DMA1->ISR, DMA_ISR_TCIF1, DMA_ISR_TCIF1);
        SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
        DMA1_Channel1->CCR = 0;
        if (!ok)
            return 0;
        if (minc)
            src += n;
        count -= n;
    }
    return 1;
}

static int fill16_dma(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    static uint16_t c;
    c = color;
    window16(x0, y0, x1, y1);
    lcd_bus_begin(1);
    int ok = dma_send(&c, (uint32_t)(x1 - x0 + 1u) * (uint32_t)(y1 - y0 + 1u), 0);
    spi_drain();
    return ok;
}

/* ---- ADC: internal temperature sensor and VREFINT ---- */

static int g_adc_ok;

static void adc_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_0;    /* PCLK/2 = 32 MHz (max 35 MHz) */
    ADC1->CR = ADC_CR_ADVREGEN;
    delay_loop(1000);                    /* > 20 us regulator start-up */
    ADC1->CR |= ADC_CR_ADCAL;
    if (!wait_flag(&ADC1->CR, ADC_CR_ADCAL, 0))
        return;
    ADC1->SMPR = 7u;                     /* 160.5 cycles = 5 us: the sensor needs >= 5 us */
    ADC1_COMMON->CCR |= ADC_CCR_TSEN | ADC_CCR_VREFEN;
    delay_loop(1000);                    /* sensor start-up */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    g_adc_ok = wait_flag(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY);
}

static uint16_t adc_read(unsigned channel)
{
    ADC1->ISR = ADC_ISR_CCRDY;
    ADC1->CHSELR = 1u << channel;
    if (!wait_flag(&ADC1->ISR, ADC_ISR_CCRDY, ADC_ISR_CCRDY))
        return 0;
    ADC1->CR |= ADC_CR_ADSTART;
    if (!wait_flag(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC))
        return 0;
    return (uint16_t)ADC1->DR;
}

/* ---- keys: raw bits, sampled every 10 ms, two equal samples ---- */

enum {
    K_LOOP = 1u << 0, K_START = 1u << 1, K_STOP = 1u << 2, K_UP = 1u << 3, K_DOWN = 1u << 4,
    K_G1 = 1u << 5, K_G2 = 1u << 6, K_POWER = 1u << 7, K_PB6 = 1u << 8, K_PB10 = 1u << 9,
};

static unsigned keys_raw(void)
{
    uint32_t b = ~GPIOB->IDR, d = ~GPIOD->IDR;   /* active low */
    return ((d >> 0) & 1u) | ((d >> 1) & 1u) << 1 | ((b >> 5) & 1u) << 2 | ((b >> 4) & 1u) << 3
         | ((d >> 3) & 1u) << 4 | ((b >> 3) & 1u) << 5 | ((d >> 2) & 1u) << 6 | ((b >> 11) & 1u) << 7
         | ((b >> 6) & 1u) << 8 | ((b >> 10) & 1u) << 9;
}

static unsigned g_keys, g_keys_prev_sample;
static uint32_t g_key_since[10];

/* Returns the keys pressed since the last call (edges of the debounced state). */
static unsigned keys_poll(void)
{
    unsigned raw = keys_raw();
    unsigned stable = raw == g_keys_prev_sample ? raw : g_keys;
    g_keys_prev_sample = raw;
    unsigned pressed = stable & ~g_keys;
    for (unsigned i = 0; i < 10u; i++)
        if (pressed & (1u << i))
            g_key_since[i] = g_ms;
    g_keys = stable;
    return pressed;
}

static uint32_t held_ms(unsigned bit_index)
{
    return (g_keys & (1u << bit_index)) ? g_ms - g_key_since[bit_index] : 0;
}

/* ---- text (classic mode) ---- */

#define ROW_Y(n) (4u + (n) * 26u)

static void label(unsigned row, const char *text, uint16_t color)
{
    classic_mode();
    gfx_fill(0, (uint16_t)ROW_Y(row), 319, (uint16_t)(ROW_Y(row) + 23u), COLOR_BLACK);
    gfx_text(4, (uint16_t)ROW_Y(row), text, color, COLOR_BLACK);
}

static void clear_screen(void)
{
    classic_mode();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
}

static char *put_ms(char *p, uint32_t t10)   /* 10 us counts as "12.34 ms" */
{
    p = fmt_uint(p, t10 / 100u);
    *p++ = '.';
    *p++ = (char)('0' + (t10 / 10u) % 10u);
    *p++ = (char)('0' + t10 % 10u);
    return fmt_str(p, " ms");
}

/* ---- pages ---- */

enum { PG_ORIENT, PG_SPEED, PG_SCROLL, PG_SLEEP, PG_LIGHT, PG_SENSE, PG_COUNT };
static const uint8_t k_madctl[4] = { 0x60, 0xA0, 0x20, 0xE0 };
static unsigned g_page, g_madctl_i;
static uint16_t g_band[320 * 8];

static void draw_orientation(void)
{
    clear_screen();
    nx_mode(k_madctl[g_madctl_i]);
    fill16(0, 0, 319, 239, COLOR_BLACK);
    fill16(0, 0, 319, 9, COLOR_RED);         /* expected along the top edge */
    fill16(0, 0, 9, 239, COLOR_GREEN);       /* expected along the left edge */
    fill16(290, 210, 319, 239, COLOR_PBLUE); /* expected in the bottom-right corner */
    fill16(40, 30, 55, 120, COLOR_WHITE);    /* an upright "F": shows any mirroring */
    fill16(40, 30, 110, 45, COLOR_WHITE);
    fill16(40, 68, 95, 83, COLOR_WHITE);
    char buf[40], *p = fmt_str(buf, "1/6 MADCTL 0x");
    fmt_hex(p, k_madctl[g_madctl_i], 2);
    label(5, buf, COLOR_YELLOW);
    label(6, "red top, green left, F upright?", COLOR_WHITE);
    label(7, "UP/DOWN: other MADCTL  LOOP: next", COLOR_GREY);
}

static void draw_speed(int run)
{
    clear_screen();
    label(0, "2/6 bus speed (full screen)", COLOR_YELLOW);
    label(8, "START: measure  LOOP: next", COLOR_GREY);
    if (!run)
        return;
    char buf[48];
    uint16_t t0 = t10us();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLUE);
    uint16_t d8 = (uint16_t)(t10us() - t0);

    nx_mode(0x60);
    t0 = t10us();
    fill16(0, 0, 319, 239, COLOR_GREY);
    uint16_t d16 = (uint16_t)(t10us() - t0);

    t0 = t10us();
    int dma_ok = fill16_dma(0, 0, 319, 239, COLOR_DARKBLUE);
    uint16_t ddma = (uint16_t)(t10us() - t0);

    for (unsigned x = 0; x < 320u; x++)       /* gradient band: red rises left to right */
        for (unsigned y = 0; y < 8u; y++)
            g_band[y * 320u + x] = (uint16_t)(((x * 31u / 319u) << 11) | (y * 7u));
    t0 = t10us();
    int band_ok = 1;
    for (unsigned y = 0; y < 240u && band_ok; y += 8u) {
        window16(0, (uint16_t)y, 319, (uint16_t)(y + 7u));
        lcd_bus_begin(1);
        band_ok = dma_send(g_band, 320u * 8u, 1);
        spi_drain();
    }
    uint16_t dband = (uint16_t)(t10us() - t0);

    label(0, "2/6 bus speed (full screen)", COLOR_YELLOW);
    put_ms(fmt_str(buf, "8-bit classic  "), d8);
    label(2, buf, COLOR_WHITE);
    put_ms(fmt_str(buf, "16-bit polled  "), d16);
    label(3, buf, COLOR_WHITE);
    put_ms(fmt_str(buf, "16-bit DMA     "), ddma);
    label(4, dma_ok ? buf : "16-bit DMA: TIMEOUT", dma_ok ? COLOR_WHITE : COLOR_RED);
    put_ms(fmt_str(buf, "DMA bands 8 px "), dband);
    label(5, band_ok ? buf : "DMA bands: TIMEOUT", band_ok ? COLOR_WHITE : COLOR_RED);
    label(6, "gradient visible above text?", COLOR_WHITE);
}

static void draw_stripes(void)
{
    static const uint16_t k_colors[4] = { COLOR_RED, COLOR_GREEN, COLOR_PBLUE, COLOR_YELLOW };
    nx_mode(0x60);
    for (unsigned i = 0; i < 32u; i++)
        fill16((uint16_t)(i * 10u), 0, (uint16_t)(i * 10u + 9u), 239, k_colors[i % 4u]);
    fill16(0, 0, 9, 239, COLOR_WHITE);       /* marker at the left edge */
}

static void draw_scroll(int run)
{
    clear_screen();
    draw_stripes();
    label(6, "3/6 scroll: white stripe moves", COLOR_YELLOW);
    label(7, "left or right? smooth?", COLOR_WHITE);
    label(8, "START: pan  LOOP: next", COLOR_GREY);
    if (!run)
        return;
    nx_mode(0x60);
    static const uint16_t k_area[3] = { 0, 320, 0 };   /* VSCRDEF: TFA, VSA, BFA */
    cmd16(0x33, k_area, 3);
    for (uint16_t s = 0; s <= 320u; s += 8u) {
        uint16_t v = (uint16_t)(s % 320u);
        cmd16(0x37, &v, 1);                  /* VSCSAD */
        wait_ms(6);
    }
    uint16_t zero = 0;
    cmd16(0x37, &zero, 1);
    cmd16(0x13, 0, 0);                       /* NORON: leave scroll mode */
}

static void draw_sleep(int run)
{
    clear_screen();
    draw_stripes();
    label(6, "4/6 sleep 2 s, display off 1 s", COLOR_YELLOW);
    label(7, "stripes intact afterwards?", COLOR_WHITE);
    label(8, "START: run  LOOP: next", COLOR_GREY);
    if (!run)
        return;
    nx_mode(0x60);
    cmd16(0x10, 0, 0);                       /* SLPIN */
    wait_ms(2000);
    cmd16(0x11, 0, 0);                       /* SLPOUT */
    wait_ms(150);
    cmd16(0x28, 0, 0);                       /* DISPOFF */
    wait_ms(1000);
    cmd16(0x29, 0, 0);                       /* DISPON */
    label(8, "done: intact? START again", COLOR_GREY);
}

static void draw_light(void)
{
    char buf[48], *p;
    clear_screen();
    label(0, "5/6 backlight TIM17 PWM", COLOR_YELLOW);
    p = fmt_str(buf, "level ");
    p = fmt_uint(p, bl_level);
    p = fmt_str(p, "/255  period ");
    p = fmt_uint(p, bl_period_us);
    fmt_str(p, " us");
    label(2, buf, COLOR_WHITE);
    label(3, "flicker? whine? lowest usable?", COLOR_WHITE);
    label(6, "UP/DOWN level  G1/G2 frequency", COLOR_GREY);
    label(7, "START: fade 0-255-0  LOOP: next", COLOR_GREY);
}

static void draw_sense_static(void)
{
    clear_screen();
    label(0, "6/6 sensors, keys, PB1", COLOR_YELLOW);
    label(8, "hold LOOP 2 s: next page", COLOR_GREY);
}

static void update_sense(void)
{
    char buf[64], *p;
    if (g_adc_ok) {
        uint16_t vref = adc_read(ADC_CH_VREF), ts = adc_read(ADC_CH_TEMP);
        if (vref) {
            uint32_t vdda = 3000u * VREF_CAL / vref;                                  /* mV */
            int32_t ts30 = (int32_t)((uint32_t)ts * vdda / 3000u);                     /* raw at 3.0 V */
            int32_t t10 = 300 + (ts30 - (int32_t)TS_CAL1) * 12000 / 4095;              /* 2.5 mV/°C */
            p = fmt_str(buf, "MCU ");
            if (t10 < 0) {
                *p++ = '-';
                t10 = -t10;
            }
            p = fmt_uint(p, (uint32_t)t10 / 10u);
            *p++ = '.';
            *p++ = (char)('0' + (uint32_t)t10 % 10u);
            p = fmt_str(p, " C  VDD ");
            p = fmt_uint(p, vdda / 1000u);
            *p++ = '.';
            p = fmt_put_fixed(p, (vdda % 1000u) / 10u, 2);
            fmt_str(p, " V");
            label(2, buf, COLOR_WHITE);
        }
    } else {
        label(2, "ADC not ready", COLOR_RED);
    }
    static const char *const k_names[10] = { "LOOP", "START", "STOP", "UP", "DOWN", "G1", "G2", "POWER", "PB6", "PB10" };
    p = fmt_str(buf, "keys:");
    for (unsigned i = 0; i < 10u; i++) {
        if (g_keys & (1u << i)) {
            *p++ = ' ';
            p = fmt_str(p, k_names[i]);
        }
    }
    label(4, buf, COLOR_WHITE);
    label(5, pin_read(GPIOB, HOST_DETECT_PIN) ? "PB1 = 1 (USB/PC)" : "PB1 = 0 (standalone)", COLOR_CYAN);
}

static void page_enter(unsigned page)
{
    g_page = page;
    switch (page) {
    case PG_ORIENT: draw_orientation(); break;
    case PG_SPEED:  draw_speed(0); break;
    case PG_SCROLL: draw_scroll(0); break;
    case PG_SLEEP:  draw_sleep(0); break;
    case PG_LIGHT:  draw_light(); break;
    default:        draw_sense_static(); break;
    }
}

static void power_off(void)
{
    backlight_set(0, bl_period_us);
    clear_screen();
    power_off_signal();
}

int main(void)
{
    clock_init();
    board_init();                     /* no classic time base in this app */
    RCC->APBENR2 |= RCC_APBENR2_TIM17EN;
    timers_init();
    __enable_irq();
    delay_loop(0x82355);
    lcd_init();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    backlight_set(128, 1000);
    dma_init();
    adc_init();
    page_enter(PG_ORIENT);

    static const uint16_t k_periods[3] = { 1600, 1000, 500 };   /* 625 Hz, 1 kHz, 2 kHz */
    unsigned period_i = 1;
    uint32_t last = g_ms, last_sense = 0, last_repeat = 0;
    for (;;) {
        while (g_ms - last < 10u) {
        }
        last = g_ms;
        unsigned k = keys_poll();

        if (held_ms(7) > 800u)
            power_off();
        if (g_page == PG_SENSE) {
            if (held_ms(0) > 2000u) {
                /* LOOP stays in the debounced state, so the still-held key is not seen as a
                 * new press on page 1 */
                page_enter(PG_ORIENT);
            } else if (g_ms - last_sense >= 200u) {
                last_sense = g_ms;
                update_sense();
            }
            continue;
        }
        if (k & K_LOOP) {
            page_enter((g_page + 1u) % PG_COUNT);
            continue;
        }
        switch (g_page) {
        case PG_ORIENT:
            if (k & (K_UP | K_DOWN)) {
                g_madctl_i = (g_madctl_i + ((k & K_UP) ? 1u : 3u)) % 4u;
                draw_orientation();
            }
            break;
        case PG_SPEED:
            if (k & K_START)
                draw_speed(1);
            break;
        case PG_SCROLL:
            if (k & K_START)
                draw_scroll(1);
            break;
        case PG_SLEEP:
            if (k & K_START)
                draw_sleep(1);
            break;
        case PG_LIGHT: {
            int step = 0;
            if (k & K_UP)
                step = 8;
            if (k & K_DOWN)
                step = -8;
            if ((held_ms(3) > 400u || held_ms(4) > 400u) && g_ms - last_repeat >= 100u) {
                last_repeat = g_ms;
                step = held_ms(3) ? 8 : -8;
            }
            if (k & (K_G1 | K_G2))
                period_i = (period_i + ((k & K_G2) ? 1u : 2u)) % 3u;
            if (step || (k & (K_G1 | K_G2))) {
                int lv = (int)bl_level + step;
                lv = lv < 0 ? 0 : lv > 255 ? 255 : lv;
                backlight_set((uint8_t)lv, k_periods[period_i]);
                draw_light();
            }
            if (k & K_START) {
                for (int lv = 0; lv <= 255; lv += 5) {
                    backlight_set((uint8_t)lv, k_periods[period_i]);
                    wait_ms(20);
                }
                for (int lv = 255; lv >= 0; lv -= 5) {
                    backlight_set((uint8_t)lv, k_periods[period_i]);
                    wait_ms(20);
                }
                backlight_set(128, k_periods[period_i]);
                draw_light();
            }
            break;
        }
        default:
            break;
        }
    }
}
