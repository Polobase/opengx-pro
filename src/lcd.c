#include "lcd.h"

#include "lcd_bus.h"

#define PANEL_VARIANT_ADDR 0x08003C00u   /* bootloader page, read only */

/* Command lists from lcd_init 0x0800A188: {command, n, data[n]}..., 0xFF = SLPOUT wait, 0x00 = end.
 * Variant B's E1 really has 15 parameters in the original; replayed as-is. */
static const uint8_t init_a[] = {
    0x36, 1, 0x00,
    0x3A, 1, 0x05,
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x56,
    0xBB, 1, 0x15,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x18,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xF0, 0x19, 0x17, 0x03, 0x04, 0x13, 0x38, 0x33, 0x4E, 0x36, 0x13, 0x13, 0x25, 0x36,
    0xE1, 14, 0xF0, 0x16, 0x18, 0x05, 0x05, 0x01, 0x38, 0x32, 0x4E, 0x38, 0x14, 0x15, 0x27, 0x38,
    0x11, 0, 0xFF,
    0x21, 0,
    0x29, 0,
    0x2C, 0,
    0x00,
};

static const uint8_t init_b[] = {
    0x36, 1, 0x00,
    0x3A, 1, 0x05,
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x1C,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x0B,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x00, 0x03, 0x09, 0x13, 0x1C, 0x3A, 0x55, 0x48, 0x18, 0x12, 0x0E, 0x19, 0x1E,
    0xE1, 15, 0xD0, 0x00, 0x03, 0x09, 0x05, 0x25, 0x3A, 0x55, 0x50, 0x3D, 0x1C, 0x1D, 0x1D, 0x1E, 0x29,
    0x11, 0, 0xFF,
    0x29, 0,
    0x2C, 0,
    0x00,
};

static inline void spi_wait_idle(void)
{
    while (SPI2->SR & SPI_SR_BSY) {
    }
}

void lcd_bus_begin(int data)
{
    spi_wait_idle();
    if (data)
        pin_high(LCD_DC_PORT, LCD_DC_PIN);
    else
        pin_low(LCD_DC_PORT, LCD_DC_PIN);
    pin_low(LCD_CS_PORT, LCD_CS_PIN);
}

void lcd_bus_end(void)
{
    spi_wait_idle();
    pin_high(LCD_CS_PORT, LCD_CS_PIN);
}

static void write_cmd(uint8_t cmd)
{
    lcd_bus_begin(0);
    lcd_bus_put(cmd);
    lcd_bus_end();
}

static void write_data(const uint8_t *data, uint32_t n)
{
    lcd_bus_begin(1);
    while (n--)
        lcd_bus_put(*data++);
    lcd_bus_end();
}

void lcd_bus_window(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1)
{
    uint8_t cols[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t rows[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    write_cmd(0x2A);
    write_data(cols, 4);
    write_cmd(0x2B);
    write_data(rows, 4);
    write_cmd(0x2C);
}

static void spi2_init(void)
{
    RCC->APBENR1 |= RCC_APBENR1_SPI2EN;
    SPI2->CR1 = 0;
    SPI2->CR2 = 7u << SPI_CR2_DS_Pos;   /* 8-bit frames */
    /* master, mode 3, PCLK/2 = 32 MHz, software NSS: same as MX_SPI2_Init */
    SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI2->CR1 |= SPI_CR1_SPE;
}

char lcd_variant_name(uint16_t variant)
{
    return (variant == 0xFF00u || variant == 0xF000u) ? 'A' : 'B';
}

uint16_t lcd_init(void)
{
    spi2_init();

    pin_low(LCD_RST_PORT, LCD_RST_PIN);
    delay_loop(0x682AA);
    pin_high(LCD_RST_PORT, LCD_RST_PIN);
    delay_loop(0x682AA);

    uint16_t variant = *(const volatile uint16_t *)PANEL_VARIANT_ADDR;
    const uint8_t *p = lcd_variant_name(variant) == 'A' ? init_a : init_b;
    while (*p) {
        uint8_t cmd = *p++, n = *p++;
        write_cmd(cmd);
        if (n) {
            write_data(p, n);
            p += n;
        }
        if (*p == 0xFF) {       /* after SLPOUT */
            delay_loop(0x271000);
            p++;
        }
    }
    return variant;
}
