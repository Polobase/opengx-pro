/* CRC-32 (nx_crc.h). */
#include "nx_crc.h"

#include "fmt.h"

#ifdef HOST_TEST
uint32_t nx_crc32(uint32_t crc, const void *buf, uint32_t len)
{
    return fmt_crc32(crc, (const uint8_t *)buf, len);
}

int nx_crc_init(void)
{
    return 0;
}
#else
#include "stm32g0xx.h"

static uint8_t s_hw;

/* ARMv6-M has no RBIT instruction. */
static uint32_t rbit32(uint32_t v)
{
    v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
    v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
    v = ((v >> 4) & 0x0F0F0F0Fu) | ((v & 0x0F0F0F0Fu) << 4);
    v = ((v >> 8) & 0x00FF00FFu) | ((v & 0x00FF00FFu) << 8);
    return (v >> 16) | (v << 16);
}

static uint32_t hw_crc32(uint32_t crc, const uint8_t *p, uint32_t len)
{
    /* zlib keeps the reflected register ~crc; the unit works MSB-first, so it starts from the
     * bit-reversed value and returns the reflected result through REV_OUT. */
    CRC->POL = 0x04C11DB7u;
    CRC->CR = CRC_CR_REV_OUT | CRC_CR_REV_IN_0;   /* 32-bit polynomial, input reversed per byte */
    CRC->INIT = rbit32(~crc);
    CRC->CR |= CRC_CR_RESET;                        /* DR = INIT */
    while (len--)
        *(volatile uint8_t *)&CRC->DR = *p++;
    return ~CRC->DR;
}

uint32_t nx_crc32(uint32_t crc, const void *buf, uint32_t len)
{
    return s_hw ? hw_crc32(crc, (const uint8_t *)buf, len) : fmt_crc32(crc, (const uint8_t *)buf, len);
}

int nx_crc_init(void)
{
    static const uint8_t check[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    RCC->AHBENR |= RCC_AHBENR_CRCEN;
    s_hw = hw_crc32(0, check, 9) == 0xCBF43926u && hw_crc32(hw_crc32(0, check, 4), check + 4, 5) == 0xCBF43926u;
    return s_hw;
}
#endif
