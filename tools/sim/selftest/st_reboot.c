/* Reboot-continue (tools/sim/selftest.py "reboot"). PB1 low: power-on -> SYSRESETREQ -> (the
 * harness pulses NRST at 2 ms) -> third boot; each boot records RCC_CSR and whether RAM survived
 * in KEEP (outside .bss, so start-up does not clear it). PB1 high: the power-on boot programs a
 * flash page, the armed power cut tears it, and the boot after the cut reports in res[]. */
#include "st.h"

#define RCC_CSR      0x40021060u
#define RCC_IOPENR   0x40021034u
#define RCC_APBENR2  0x40021040u
#define GPIOA_BSRR   0x50000018u
#define GPIOB_MODER  0x50000400u
#define GPIOB_IDR    0x50000410u
#define SCB_AIRCR    0xE000ED0Cu
#define SPI1         0x40013000u
#define MAGIC        0x5EB007EDu
#define KEEP         ((volatile uint32_t *)0x20001000u)
#define SCRIBBLED    (*(volatile uint32_t *)0x20001800u)   /* inside the harness's boot_scribble range */
#define RMVF         (1u << 23)
#define PWRRSTF      (1u << 27)
#define SFTRSTF      (1u << 28)

enum { K_MAGIC, K_CSR1, K_CSR1_RMVF, K_CSR2, K_MAGIC2, K_SCRIB2, K_CSR3, K_MAGIC3 };

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
};

static uint8_t spi_x(uint8_t v)
{
    while (!(REG(SPI1 + 0x08u) & 2u)) {
    }
    REG8(SPI1 + 0x0Cu) = v;
    while (!(REG(SPI1 + 0x08u) & 1u)) {
    }
    return REG8(SPI1 + 0x0Cu);
}

static void cs(int high)
{
    REG(GPIOA_BSRR) = high ? 1u << 1 : 1u << 17;
}

static void send_addr(uint8_t cmd, uint32_t a)
{
    spi_x(cmd);
    spi_x((uint8_t)(a >> 16));
    spi_x((uint8_t)(a >> 8));
    spi_x((uint8_t)a);
}

static uint8_t flash_byte(uint32_t a)
{
    cs(0);
    send_addr(0x03, a);
    uint8_t v = spi_x(0xFF);
    cs(1);
    return v;
}

static void flash_program_zeros(uint32_t a)
{
    cs(0);
    spi_x(0x06);                                 /* write enable */
    cs(1);
    cs(0);
    send_addr(0x02, a);
    for (unsigned i = 0; i < 256u; i++)
        spi_x(0x00);
    cs(1);                                       /* the page program starts now */
    for (;;)
        __asm volatile("wfi");                   /* the armed power cut comes halfway through */
}

int main(void)
{
    uint32_t csr = REG(RCC_CSR);
    REG(RCC_IOPENR) |= 3u;                       /* GPIOA, GPIOB */
    REG(GPIOB_MODER) &= ~(3u << 2);              /* PB1 input: high selects the power-cut run */
    if (csr & PWRRSTF) {
        if (REG(GPIOB_IDR) & 2u) {               /* power-cut run */
            REG(RCC_APBENR2) |= 1u << 12;        /* SPI1 */
            cs(1);
            REG(SPI1 + 0x04u) = (7u << 8) | (1u << 12);
            REG(SPI1) = (1u << 2) | (3u << 8) | (1u << 6);
            if (flash_byte(0x1000u) == 0xFFu) {  /* first power-on: the page is still erased */
                KEEP[K_MAGIC] = MAGIC;
                flash_program_zeros(0x1000u);
            }
            res[0] = csr;
            res[1] = KEEP[K_MAGIC] == MAGIC;     /* 0: RAM was lost in the power cut */
            res[15] = DONE;
            return 0;
        }
        KEEP[K_MAGIC] = MAGIC;                   /* first boot of the reboot run */
        KEEP[K_CSR1] = csr;
        SCRIBBLED = 0x12345678u;
        REG(RCC_CSR) |= RMVF;
        KEEP[K_CSR1_RMVF] = REG(RCC_CSR);
        REG(SCB_AIRCR) = 0x05FA0004u;            /* SYSRESETREQ */
        for (;;) {
        }
    }
    if (csr & SFTRSTF) {                         /* second boot */
        KEEP[K_CSR2] = csr;
        KEEP[K_MAGIC2] = KEEP[K_MAGIC] == MAGIC;
        KEEP[K_SCRIB2] = SCRIBBLED;
        REG(RCC_CSR) |= RMVF;
        for (;;)
            __asm volatile("wfi");               /* the harness pulses NRST */
    }
    KEEP[K_CSR3] = csr;                          /* third boot: PINRSTF only */
    KEEP[K_MAGIC3] = KEEP[K_MAGIC] == MAGIC;
    res[15] = DONE;
    return 0;
}
