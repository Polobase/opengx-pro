/* DMA1 + DMAMUX1 (tools/sim/selftest.py "dma"): 128 16-bit frames memory -> SPI2 into a 16 x 8
 * LCD window (MADCTL 0xA0) with half/complete interrupts on IRQ 9, and SPI1 full duplex through
 * two channels (TX channel 3, RX channel 2, IRQ 10) reading the flash's JEDEC ID. */
#include "st.h"

#define RCC_IOPENR    0x40021034u
#define RCC_AHBENR    0x40021038u
#define RCC_APBENR1   0x4002103Cu
#define RCC_APBENR2   0x40021040u
#define GPIOA_BSRR    0x50000018u
#define GPIOC_BSRR    0x50000818u
#define SPI1          0x40013000u
#define SPI2          0x40003800u
#define SPI_CR2       0x04u
#define SPI_SR        0x08u
#define SPI_DR        0x0Cu
#define SPI_ON        ((1u << 2) | (3u << 8) | (1u << 6))   /* MSTR, SSM + SSI, SPE, fPCLK/2 */
#define DMA_ISR       0x40020000u
#define DMA_IFCR      0x40020004u
#define DMA_CCR(n)    (0x40020008u + 0x14u * ((n) - 1u))
#define DMA_CNDTR(n)  (DMA_CCR(n) + 4u)
#define DMA_CPAR(n)   (DMA_CCR(n) + 8u)
#define DMA_CMAR(n)   (DMA_CCR(n) + 12u)
#define DMAMUX_CR(n)  (0x40020800u + 4u * ((n) - 1u))        /* DMAMUX channel n-1 feeds DMA channel n */
#define REQ_SPI1_RX   16u                                     /* RM0454 DMAMUX request table (verify) */
#define REQ_SPI1_TX   17u
#define REQ_SPI2_TX   19u
#define CCR_EN        (1u << 0)
#define CCR_TCIE      (1u << 1)
#define CCR_HTIE      (1u << 2)
#define CCR_DIR       (1u << 4)                               /* memory -> peripheral */
#define CCR_MINC      (1u << 7)
#define CCR_16BIT     ((1u << 8) | (1u << 10))                /* PSIZE and MSIZE 16 bits */
#define SYST_CSR      0xE000E010u
#define SYST_RVR      0xE000E014u
#define SYST_CVR      0xE000E018u
#define NVIC_ISER     0xE000E100u

static uint16_t pix[128];
static uint8_t tx4[4], rx4[4];
static volatile uint32_t tc1, ht1, t_tc, tc2;

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t isr = REG(DMA_ISR);
    if (isr & (1u << 2))                         /* HTIF1 */
        ht1++;
    if (isr & (1u << 1)) {                       /* TCIF1 */
        tc1++;
        t_tc = REG(SYST_CVR);
    }
    REG(DMA_IFCR) = 1u;                          /* CGIF1: all channel-1 flags */
}

void DMA1_Channel2_3_IRQHandler(void)
{
    if (REG(DMA_ISR) & (1u << 5))               /* TCIF2 */
        tc2++;
    REG(DMA_IFCR) = 0xFFu << 4;                  /* channels 2 and 3 */
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [16 + 9] = DMA1_Channel1_IRQHandler, [16 + 10] = DMA1_Channel2_3_IRQHandler,
};

static void spi_idle(uint32_t spi)
{
    while (REG(spi + SPI_SR) & (1u << 7)) {     /* BSY */
    }
}

static void spi_put8(uint32_t spi, uint8_t v)
{
    while (!(REG(spi + SPI_SR) & 2u)) {         /* TXE */
    }
    REG8(spi + SPI_DR) = v;
}

static void lcd_cmd(uint8_t c)                   /* D/C low for the command, high afterwards */
{
    spi_idle(SPI2);
    REG(GPIOC_BSRR) = 1u << 22;
    spi_put8(SPI2, c);
    spi_idle(SPI2);
    REG(GPIOC_BSRR) = 1u << 6;
}

static void lcd_range(uint8_t cmd, uint16_t a, uint16_t b)
{
    lcd_cmd(cmd);
    spi_put8(SPI2, (uint8_t)(a >> 8));
    spi_put8(SPI2, (uint8_t)a);
    spi_put8(SPI2, (uint8_t)(b >> 8));
    spi_put8(SPI2, (uint8_t)b);
}

int main(void)
{
    REG(RCC_IOPENR) |= (1u << 0) | (1u << 2);   /* GPIOA, GPIOC */
    REG(RCC_AHBENR) |= 1u;                       /* DMA1 + DMAMUX1 */
    REG(RCC_APBENR1) |= 1u << 14;                /* SPI2 */
    REG(RCC_APBENR2) |= 1u << 12;                /* SPI1 */
    REG(GPIOA_BSRR) = (1u << 1) | (1u << 8);     /* flash and LCD chip selects high */
    REG(SYST_RVR) = 0xFFFFFFu;                   /* free-running down-counter at 64 MHz */
    REG(SYST_CVR) = 0;
    REG(SYST_CSR) = 5u;
    REG(NVIC_ISER) = (1u << 9) | (1u << 10);

    /* 1. memory -> SPI2: 64 red then 64 green 16-bit frames into logical X 0..15, Y 0..7 */
    REG(SPI2 + SPI_CR2) = 7u << 8;
    REG(SPI2) = SPI_ON;
    REG(GPIOA_BSRR) = 1u << 24;                  /* LCD CS (PA8) low */
    lcd_cmd(0x11);                               /* sleep out */
    lcd_cmd(0x29);                               /* display on */
    lcd_cmd(0x21);                               /* inversion on (panel variant A) */
    lcd_cmd(0x36);
    spi_put8(SPI2, 0xA0);                        /* MADCTL: landscape */
    lcd_range(0x2A, 0, 15);
    lcd_range(0x2B, 0, 7);
    lcd_cmd(0x2C);                               /* memory write; D/C stays high */
    spi_idle(SPI2);
    REG(SPI2) = 0;                               /* SPE off to change the frame size */
    REG(SPI2 + SPI_CR2) = 15u << 8;              /* DS = 16 */
    REG(SPI2) = SPI_ON;
    for (unsigned i = 0; i < 128u; i++)
        pix[i] = i < 64u ? 0xF800u : 0x07E0u;
    REG(DMAMUX_CR(1)) = REQ_SPI2_TX;
    REG(DMA_CPAR(1)) = SPI2 + SPI_DR;
    REG(DMA_CMAR(1)) = (uint32_t)pix;
    REG(DMA_CNDTR(1)) = 128u;
    REG(DMA_CCR(1)) = CCR_DIR | CCR_MINC | CCR_16BIT | CCR_TCIE | CCR_HTIE | CCR_EN;
    uint32_t t0 = REG(SYST_CVR);
    REG(SPI2 + SPI_CR2) |= 1u << 1;              /* TXDMAEN: the transfer starts */
    while (!tc1)
        __asm volatile("wfi");
    spi_idle(SPI2);
    REG(SPI2 + SPI_CR2) &= ~(1u << 1);
    REG(GPIOA_BSRR) = 1u << 8;
    res[0] = tc1;
    res[1] = ht1;
    res[2] = (t0 - t_tc) & 0xFFFFFFu;
    res[3] = REG(DMA_CNDTR(1));
    REG(DMA_CCR(1)) = 0;

    /* 2. SPI1 full duplex: channel 3 sends 9F 00 00 00, channel 2 stores the answer */
    tx4[0] = 0x9F;
    REG(SPI1 + SPI_CR2) = (7u << 8) | (1u << 12);   /* DS = 8, RXNE per byte */
    REG(SPI1) = SPI_ON;
    REG(DMAMUX_CR(2)) = REQ_SPI1_RX;
    REG(DMA_CPAR(2)) = SPI1 + SPI_DR;
    REG(DMA_CMAR(2)) = (uint32_t)rx4;
    REG(DMA_CNDTR(2)) = 4u;
    REG(DMA_CCR(2)) = CCR_MINC | CCR_TCIE | CCR_EN;
    REG(DMAMUX_CR(3)) = REQ_SPI1_TX;
    REG(DMA_CPAR(3)) = SPI1 + SPI_DR;
    REG(DMA_CMAR(3)) = (uint32_t)tx4;
    REG(DMA_CNDTR(3)) = 4u;
    REG(DMA_CCR(3)) = CCR_DIR | CCR_MINC | CCR_EN;
    REG(GPIOA_BSRR) = 1u << 17;                  /* flash CS (PA1) low */
    REG(SPI1 + SPI_CR2) |= 1u;                   /* RXDMAEN first, */
    REG(SPI1 + SPI_CR2) |= 1u << 1;              /* then TXDMAEN */
    while (!tc2)
        __asm volatile("wfi");
    spi_idle(SPI1);
    REG(GPIOA_BSRR) = 1u << 1;
    res[4] = rx4[0] | (uint32_t)rx4[1] << 8 | (uint32_t)rx4[2] << 16 | (uint32_t)rx4[3] << 24;
    res[5] = tc2;
    res[15] = DONE;
    return 0;
}
