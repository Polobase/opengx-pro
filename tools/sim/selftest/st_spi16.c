/* SPI data size (tools/sim/selftest.py "spi16"): with DS = 16 a 16-bit DR write is one frame,
 * high byte first on the wire; with DS = 8 it packs two frames, low byte first (the classic
 * firmware's mode). The W25Q16 on SPI1 answers the JEDEC ID command 0x9F with EF 40 15. */
#include "st.h"

#define RCC_IOPENR   0x40021034u
#define RCC_APBENR2  0x40021040u
#define GPIOA_BSRR   0x50000018u
#define SPI1         0x40013000u
#define SPI_CR2      0x04u
#define SPI_SR       0x08u
#define SPI_DR       0x0Cu

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
};

static uint16_t xfer16(uint16_t v)
{
    while (!(REG(SPI1 + SPI_SR) & 2u)) {        /* TXE */
    }
    REG16(SPI1 + SPI_DR) = v;
    while (!(REG(SPI1 + SPI_SR) & 1u)) {        /* RXNE: 16 bits received (FRXTH = 0) */
    }
    return REG16(SPI1 + SPI_DR);
}

static void cs(int high)
{
    REG(GPIOA_BSRR) = high ? 1u << 1 : 1u << 17;
}

static void spi1_frames(unsigned bits)
{
    REG(SPI1) = 0;
    REG(SPI1 + SPI_CR2) = (bits - 1u) << 8;
    REG(SPI1) = (1u << 2) | (3u << 8) | (1u << 6);   /* MSTR, SSM + SSI, SPE, fPCLK/2 */
}

int main(void)
{
    REG(RCC_IOPENR) |= 1u;                       /* GPIOA */
    REG(RCC_APBENR2) |= 1u << 12;                /* SPI1 */
    cs(1);
    spi1_frames(16);
    cs(0);
    res[0] = xfer16(0x9F00u);                    /* 0x9F, 0x00 -> 0xFF, 0xEF */
    res[1] = xfer16(0x0000u);                    /* -> 0x40, 0x15 */
    cs(1);
    spi1_frames(8);
    cs(0);
    res[2] = xfer16(0x009Fu);                    /* 0x9F, 0x00 -> 0xFF in bits 0-7, 0xEF in 8-15 */
    res[3] = xfer16(0x0000u);
    cs(1);
    res[15] = DONE;
    return 0;
}
