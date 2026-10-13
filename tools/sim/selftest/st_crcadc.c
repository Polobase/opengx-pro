/* CRC unit and ADC (tools/sim/selftest.py "crcadc"): CRC-32/MPEG-2 with the defaults, zlib CRC-32
 * with reversed input/output, CRC-16/CCITT-FALSE with a 16-bit polynomial, INIT loading DR, IDR;
 * ADC calibration, enable, channel selection and the VREFINT / temperature readings. */
#include "st.h"

#define RCC_AHBENR   0x40021038u
#define RCC_APBENR2  0x40021040u
#define CRC_DR       0x40023000u
#define CRC_IDR      0x40023004u
#define CRC_CR       0x40023008u
#define CRC_INIT     0x40023010u
#define CRC_POL      0x40023014u
#define ADC_ISR      0x40012400u
#define ADC_CR       0x40012408u
#define ADC_CHSELR   0x40012428u
#define ADC_DR       0x40012440u
#define ADC_CCR      0x40012708u
#define TS_CAL1      (*(const volatile uint16_t *)0x1FFF75A8u)
#define VREFINT_CAL  (*(const volatile uint16_t *)0x1FFF75AAu)

static const char digits[] = "123456789";

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
};

static uint32_t wait(uint32_t reg, uint32_t mask, uint32_t want)
{
    uint32_t n = 0;
    while ((REG(reg) & mask) != want && n < 100000u)
        n++;
    return n < 100000u;
}

static uint32_t adc_read(uint32_t ch)
{
    REG(ADC_ISR) = 1u << 13;                     /* clear CCRDY */
    REG(ADC_CHSELR) = 1u << ch;
    if (!wait(ADC_ISR, 1u << 13, 1u << 13))
        return 0;
    REG(ADC_CR) |= 1u << 2;                      /* ADSTART */
    if (!wait(ADC_ISR, 1u << 2, 1u << 2))       /* EOC */
        return 0;
    return REG(ADC_DR);
}

int main(void)
{
    REG(RCC_AHBENR) |= 1u << 12;                 /* CRC */
    REG(CRC_CR) = 1u;                            /* defaults: CRC-32/MPEG-2, byte by byte */
    for (unsigned i = 0; i < 9u; i++)
        REG8(CRC_DR) = (uint8_t)digits[i];
    res[0] = REG(CRC_DR);
    REG(CRC_CR) = (3u << 5) | (1u << 7) | 1u;   /* zlib: input reversed by word, output reversed */
    REG(CRC_DR) = 0x34333231u;                   /* "1234" as a little-endian word */
    REG(CRC_DR) = 0x38373635u;                   /* "5678" */
    REG8(CRC_DR) = '9';
    res[1] = REG(CRC_DR) ^ 0xFFFFFFFFu;
    REG(CRC_POL) = 0x1021u;                      /* CRC-16/CCITT-FALSE */
    REG(CRC_INIT) = 0xFFFFu;
    REG(CRC_CR) = (1u << 3) | 1u;
    for (unsigned i = 0; i < 9u; i++)
        REG8(CRC_DR) = (uint8_t)digits[i];
    res[2] = REG(CRC_DR) & 0xFFFFu;
    REG(CRC_CR) = 0;
    REG(CRC_POL) = 0x04C11DB7u;
    REG(CRC_INIT) = 0x12345678u;                 /* also loads DR */
    res[3] = REG(CRC_DR);
    REG(CRC_IDR) = 0xA5u;
    res[4] = REG(CRC_IDR);

    REG(RCC_APBENR2) |= 1u << 20;                /* ADC */
    REG(ADC_CR) = 1u << 28;                      /* ADVREGEN */
    REG(ADC_CR) |= 1u << 31;                     /* ADCAL */
    res[5] = wait(ADC_CR, 1u << 31, 0) && (REG(ADC_ISR) & (1u << 11)) != 0;   /* done, EOCAL */
    REG(ADC_CCR) |= (1u << 22) | (1u << 23);     /* VREFEN, TSEN */
    REG(ADC_ISR) = 1u;
    REG(ADC_CR) |= 1u;                           /* ADEN */
    res[6] = wait(ADC_ISR, 1u, 1u);              /* ADRDY */
    uint32_t vref = adc_read(13), ts = adc_read(12);
    res[7] = vref;
    res[8] = ts;
    res[9] = vref ? 3000u * VREFINT_CAL / vref : 0u;   /* VDDA in mV */
    res[10] = TS_CAL1;
    res[11] = REG(ADC_ISR) & (1u << 2);          /* reading DR cleared EOC */
    res[15] = DONE;
    return 0;
}
