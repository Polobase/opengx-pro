#include "board.h"

uint32_t SystemCoreClock = 16000000u;

enum { MODE_IN = 0, MODE_OUT = 1, MODE_AF = 2 };
enum { PULL_NONE = 0, PULL_UP = 1, PULL_DOWN = 2 };
enum { SPEED_LOW = 0, SPEED_HIGH = 2, SPEED_VHIGH = 3 };

static void pin_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t speed, uint32_t pull, uint32_t af)
{
    uint32_t s2 = pin * 2u;
    port->OTYPER &= ~(1u << pin);
    port->OSPEEDR = (port->OSPEEDR & ~(3u << s2)) | (speed << s2);
    port->PUPDR = (port->PUPDR & ~(3u << s2)) | (pull << s2);
    if (mode == MODE_AF) {
        volatile uint32_t *afr = &port->AFR[pin >> 3];
        uint32_t s4 = (pin & 7u) * 4u;
        *afr = (*afr & ~(0xFu << s4)) | (af << s4);
    }
    port->MODER = (port->MODER & ~(3u << s2)) | (mode << s2);
}

int clock_init(void)
{
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_1 | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_1) {
    }

    /* The bootloader may hand over running on the PLL: go back to HSI before reprogramming it. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE);
    while (RCC->CFGR & RCC_CFGR_SWS) {
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) {
    }

    RCC->CR |= RCC_CR_HSEON;
    int hse = 0;
    for (uint32_t i = 0; i < 400000u; i++) {   /* >= 100 ms at 16 MHz */
        if (RCC->CR & RCC_CR_HSERDY) {
            hse = 1;
            break;
        }
    }

    /* 8 MHz HSE x16 /2, or 16 MHz HSI x8 /2: VCO 128 MHz, PLLR 64 MHz (original: HSE, N=16, R=/2) */
    RCC->PLLCFGR = (hse ? RCC_PLLCFGR_PLLSRC_HSE : RCC_PLLCFGR_PLLSRC_HSI)
                 | ((hse ? 16u : 8u) << RCC_PLLCFGR_PLLN_Pos)
                 | (1u << RCC_PLLCFGR_PLLR_Pos)
                 | RCC_PLLCFGR_PLLREN;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }
    RCC->CFGR |= RCC_CFGR_SW_1;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_1) {
    }
    if (!hse)
        RCC->CR &= ~RCC_CR_HSEON;

    SystemCoreClock = 64000000u;
    return hse;
}

static void gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN | RCC_IOPENR_GPIOFEN;

    /* Release the UCPD dead-battery pull-downs on PA8, PB15, PD0 and PD2 (original HAL_MspInit). */
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE | SYSCFG_CFGR1_UCPD2_STROBE;

    /* Initial levels are written before a pin becomes an output, as in MX_GPIO_Init. */
    pin_high(GPIOA, FLASH_CS_PIN);
    pin_high(GPIOA, LCD_CS_PIN);
    pin_low(GPIOB, 0);
    pin_low(GPIOB, HANDSHAKE_PIN);
    pin_low(GPIOB, PB7_PWM_PIN);
    pin_low(GPIOB, BACKLIGHT_PIN);
    pin_low(GPIOC, LCD_DC_PIN);
    pin_high(GPIOC, LCD_RST_PIN);

    pin_config(GPIOA, FLASH_CS_PIN, MODE_OUT, SPEED_VHIGH, PULL_NONE, 0);
    pin_config(GPIOA, 2, MODE_AF, SPEED_HIGH, PULL_NONE, 1);    /* USART2_TX */
    pin_config(GPIOA, 3, MODE_AF, SPEED_HIGH, PULL_UP, 1);      /* USART2_RX */
    pin_config(GPIOA, 5, MODE_AF, SPEED_VHIGH, PULL_NONE, 0);   /* SPI1_SCK */
    pin_config(GPIOA, 6, MODE_AF, SPEED_VHIGH, PULL_NONE, 0);   /* SPI1_MISO */
    pin_config(GPIOA, 7, MODE_AF, SPEED_VHIGH, PULL_NONE, 0);   /* SPI1_MOSI */
    pin_config(GPIOA, LCD_CS_PIN, MODE_OUT, SPEED_VHIGH, PULL_NONE, 0);
    pin_config(GPIOA, 9, MODE_AF, SPEED_HIGH, PULL_NONE, 1);    /* USART1_TX */
    pin_config(GPIOA, 10, MODE_AF, SPEED_HIGH, PULL_UP, 1);     /* USART1_RX */

    pin_config(GPIOB, 0, MODE_OUT, SPEED_LOW, PULL_DOWN, 0);    /* unknown, kept low */
    pin_config(GPIOB, HOST_DETECT_PIN, MODE_IN, SPEED_LOW, PULL_UP, 0);
    pin_config(GPIOB, HANDSHAKE_PIN, MODE_OUT, SPEED_LOW, PULL_NONE, 0);
    pin_config(GPIOB, 3, MODE_IN, SPEED_LOW, PULL_UP, 0);       /* key G1 */
    pin_config(GPIOB, 4, MODE_IN, SPEED_LOW, PULL_UP, 0);       /* key UP */
    pin_config(GPIOB, 5, MODE_IN, SPEED_LOW, PULL_UP, 0);       /* key STOP */
    pin_config(GPIOB, 6, MODE_IN, SPEED_LOW, PULL_UP, 0);       /* unused input */
    pin_config(GPIOB, PB7_PWM_PIN, MODE_OUT, SPEED_LOW, PULL_NONE, 0);
    pin_config(GPIOB, 8, MODE_AF, SPEED_LOW, PULL_NONE, 4);     /* USART3_TX */
    pin_config(GPIOB, 9, MODE_AF, SPEED_LOW, PULL_UP, 4);       /* USART3_RX */
    pin_config(GPIOB, 10, MODE_IN, SPEED_LOW, PULL_UP, 0);      /* unused input */
    pin_config(GPIOB, 11, MODE_IN, SPEED_LOW, PULL_UP, 0);      /* key POWER */
    pin_config(GPIOB, BACKLIGHT_PIN, MODE_OUT, SPEED_LOW, PULL_NONE, 0);
    pin_config(GPIOB, 13, MODE_AF, SPEED_VHIGH, PULL_NONE, 0);  /* SPI2_SCK */
    pin_config(GPIOB, 15, MODE_AF, SPEED_VHIGH, PULL_NONE, 0);  /* SPI2_MOSI */

    pin_config(GPIOC, LCD_DC_PIN, MODE_OUT, SPEED_VHIGH, PULL_NONE, 0);
    pin_config(GPIOC, LCD_RST_PIN, MODE_OUT, SPEED_HIGH, PULL_NONE, 0);

    for (uint32_t pin = 0; pin < 4; pin++)                      /* keys LOOP, START, G2, DOWN */
        pin_config(GPIOD, pin, MODE_IN, SPEED_LOW, PULL_UP, 0);
}

/* Time base and soft PWM: the classic apps link board_tim3.c (TIM3 10 kHz, as the original);
 * the nx firmware brings its own drivers. */
__attribute__((weak)) void board_timebase_init(void)
{
}

void board_init(void)
{
    /* Start every peripheral we use from its reset state, whatever the bootloader left behind. */
    RCC->APBRSTR1 |= RCC_APBRSTR1_TIM3RST | RCC_APBRSTR1_SPI2RST | RCC_APBRSTR1_USART2RST | RCC_APBRSTR1_USART3RST;
    RCC->APBRSTR1 &= ~(RCC_APBRSTR1_TIM3RST | RCC_APBRSTR1_SPI2RST | RCC_APBRSTR1_USART2RST | RCC_APBRSTR1_USART3RST);
    RCC->APBRSTR2 |= RCC_APBRSTR2_SPI1RST | RCC_APBRSTR2_USART1RST;
    RCC->APBRSTR2 &= ~(RCC_APBRSTR2_SPI1RST | RCC_APBRSTR2_USART1RST);

    gpio_init();
    board_timebase_init();
}

/* aligned(8): the delay constants (13.3 ms key scan, 40 ms :ok wait, 4 s power-off) assume this
 * loop runs from one 64-bit flash line, like the original's; keep the whole body in one line so
 * a later change elsewhere in this file can't push it across a line boundary again. */
__attribute__((naked, noinline, aligned(8))) void delay_loop(uint32_t iterations)
{
    (void)iterations;
    __asm volatile(
        "nop\n"
        "1: subs r0, r0, #1\n"
        "cmp r0, #0\n"
        "bne 1b\n"
        "bx lr\n");
}

void power_off_signal(void)
{
    /* Apps that start the independent watchdog (nx) reload it every 256 periods (~6.8 ms), so an
     * "off but still powered" unit never reboots. One loop for all apps: without CFG_IWDG it is
     * exactly the classic loop. */
#ifdef CFG_IWDG
    unsigned n = 0;
#endif
    for (;;) {
        pin_low(GPIOB, HANDSHAKE_PIN);
        delay_loop(213);
        pin_high(GPIOB, HANDSHAKE_PIN);
        delay_loop(213);
#ifdef CFG_IWDG
        if ((n++ & 0xFFu) == 0)
            IWDG->KR = 0xAAAAu;
#endif
    }
}
