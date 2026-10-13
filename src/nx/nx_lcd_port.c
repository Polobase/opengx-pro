/* SPI2 + DMA1 channel 1 below nx_lcd.c (nx_lcd_port.h), device only. After lcd_init() the bus
 * belongs to nx: CS stays low, DC changes only while the bus is idle (FIFO empty, not busy), the
 * DMA interrupt (priority 2, below the UARTs and SysTick) starts the next queued transfer. */
#include "nx_lcd_port.h"

#include "board.h"
#include "lcd_bus.h"
#include "nx_config.h"
#include "nx_lcd.h"

#define SPIN_MAX 20000u   /* one flag wait: far above the 2 frames a FIFO holds (1 us) */

static volatile uint8_t s_busy;

static void wait_idle(void)
{
    for (uint32_t n = 0; n < SPIN_MAX; n++)
        if (!(SPI2->SR & (SPI_SR_FTLVL | SPI_SR_BSY)))
            return;
}

static inline void wait_txe(void)
{
    for (uint32_t n = 0; n < SPIN_MAX && !(SPI2->SR & SPI_SR_TXE); n++) {
    }
}

static inline void put16(uint16_t v)   /* one 16-bit value, high byte first on the wire */
{
    wait_txe();
#if NX_LCD_DS16
    *(volatile uint16_t *)&SPI2->DR = v;
#else
    *(volatile uint8_t *)&SPI2->DR = (uint8_t)(v >> 8);
    wait_txe();
    *(volatile uint8_t *)&SPI2->DR = (uint8_t)v;
#endif
}

void nx_lcd_port_init(void)
{
    /* MADCTL has a single parameter byte: send it while the bus is still in 8-bit mode */
    lcd_bus_begin(0);
    lcd_bus_put(NX_ST_MADCTL);
    lcd_bus_end();
    lcd_bus_begin(1);
    lcd_bus_put(NX_LCD_MADCTL);
    lcd_bus_end();
#if NX_LCD_DS16
    wait_idle();   /* SPE may only go off once the last 8-bit frame has left the FIFO */
    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR2 = 15u << SPI_CR2_DS_Pos;   /* 16-bit frames, MSB first */
    SPI2->CR1 |= SPI_CR1_SPE;
#endif
    pin_high(LCD_DC_PORT, LCD_DC_PIN);
    pin_low(LCD_CS_PORT, LCD_CS_PIN);    /* nx is the only user of SPI2 from here on */

    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel1->CCR = 0;
    DMAMUX1_Channel0->CCR = NX_LCD_DMAREQ;   /* DMAMUX channel 0 feeds DMA1 channel 1 */
    DMA1_Channel1->CPAR = (uint32_t)&SPI2->DR;
    DMA1->IFCR = DMA_IFCR_CGIF1;
    s_busy = 0;
    NVIC_SetPriority(DMA1_Channel1_IRQn, 2);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void nx_lcd_port_cmd(uint8_t cmd, const uint16_t *args, unsigned n)
{
    wait_idle();
    pin_low(LCD_DC_PORT, LCD_DC_PIN);
#if NX_LCD_DS16
    put16(cmd);                          /* frame 0x00cc: NOP, then the command */
#else
    wait_txe();
    *(volatile uint8_t *)&SPI2->DR = cmd;
#endif
    wait_idle();
    pin_high(LCD_DC_PORT, LCD_DC_PIN);
    while (n--)
        put16(*args++);
}

void nx_lcd_port_pixels(const uint16_t *px, uint32_t n, int inc)
{
#if NX_LCD_DMA
    DMA1_Channel1->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CGIF1;
    DMA1_Channel1->CMAR = (uint32_t)px;
    DMA1_Channel1->CNDTR = n;
    s_busy = 1;
    DMA1_Channel1->CCR = DMA_CCR_DIR | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0 | (inc ? DMA_CCR_MINC : 0u)
                       | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_EN;
    SPI2->CR2 |= SPI_CR2_TXDMAEN;
#else
    volatile uint16_t *dr = (volatile uint16_t *)&SPI2->DR;
    while (n--) {
        wait_txe();
        *dr = *px;                       /* 16-bit frame, or two packed 8-bit frames */
        px += inc ? 1 : 0;
    }
#endif
}

int nx_lcd_port_busy(void)
{
    return s_busy;
}

void nx_lcd_port_lock(void)
{
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);
}

void nx_lcd_port_unlock(void)
{
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* Transfer complete (or a bus error, which ends the transfer the same way): the last frames are
 * still in the SPI FIFO, nx_lcd_port_cmd waits for them before DC changes. */
void DMA1_Channel1_IRQHandler(void)
{
    DMA1->IFCR = DMA_IFCR_CGIF1;
    DMA1_Channel1->CCR = 0;
    SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
    s_busy = 0;
    nx_lcd_irq();
}
