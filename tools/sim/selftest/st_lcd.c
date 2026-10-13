/* ST7789 model (tools/sim/selftest.py "lcd"): the same corner pattern drawn the classic way
 * (8-bit frames, MADCTL 0, rotated coordinates) and the nx way (16-bit frames with NOP-prefixed
 * commands, MADCTL 0xA0), hardware scrolling, NORON, sleep, MADCTL.RGB and a 25 % backlight
 * (TIM17 PWM on PB12, 1 kHz). st_shot(id, name) marks each moment the harness takes a
 * screenshot (r0 = id, r1 = name, as tools/emu.py --shot-hook expects). */
#include "st.h"

#define RCC_IOPENR   0x40021034u
#define RCC_APBENR1  0x4002103Cu
#define RCC_APBENR2  0x40021040u
#define GPIOA_BSRR   0x50000018u
#define GPIOB_BSRR   0x50000418u
#define GPIOC_BSRR   0x50000818u
#define SPI2         0x40003800u
#define TIM17        0x40014800u
#define SYST_CSR     0xE000E010u
#define SYST_RVR     0xE000E014u
#define SYST_CVR     0xE000E018u
#define NVIC_ISER    0xE000E100u

typedef void (*fill_fn)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

static volatile uint32_t ms, bl_high;

void SysTick_Handler(void)
{
    ms++;
}

void TIM17_IRQHandler(void)                     /* PB12: 250 us on, 750 us off */
{
    REG(TIM17 + 0x10u) = ~1u;
    bl_high ^= 1u;
    REG(GPIOB_BSRR) = bl_high ? 1u << 12 : 1u << 28;
    REG(TIM17 + 0x2Cu) = bl_high ? 249u : 749u;
}

__attribute__((section(".isr_vector"), used)) const void *const vectors[48] = {
    [0] = &_estack, [1] = Reset_Handler, [2] = Default_Handler, [3] = Default_Handler,
    [15] = SysTick_Handler, [16 + 22] = TIM17_IRQHandler,
};

__attribute__((noinline, used)) void st_shot(unsigned id, const char *name)
{
    (void)id;
    (void)name;
    __asm volatile("" ::: "memory");             /* the harness hooks this address */
}

static void wait_ms(uint32_t n)
{
    uint32_t t = ms;
    while (ms - t < n)
        __asm volatile("wfi");
}

static void idle(void)
{
    while (REG(SPI2 + 0x08u) & (1u << 7)) {     /* BSY */
    }
}

static void put8(uint8_t v)
{
    while (!(REG(SPI2 + 0x08u) & 2u)) {
    }
    REG8(SPI2 + 0x0Cu) = v;
}

static void put16(uint16_t v)
{
    while (!(REG(SPI2 + 0x08u) & 2u)) {
    }
    REG16(SPI2 + 0x0Cu) = v;
}

static void dc(int data)
{
    idle();
    REG(GPIOC_BSRR) = data ? 1u << 6 : 1u << 22;
}

static void frames(unsigned bits)
{
    idle();
    REG(SPI2) = 0;
    REG(SPI2 + 0x04u) = (bits - 1u) << 8;
    REG(SPI2) = (1u << 2) | (3u << 8) | (1u << 6);   /* MSTR, SSM + SSI, SPE, fPCLK/2 */
}

static void cmd8(uint8_t c)
{
    dc(0);
    put8(c);
    dc(1);
}

static void cmd16(uint8_t c)                     /* frame 0x00cc: NOP, then the command */
{
    dc(0);
    put16(c);
    dc(1);
}

static void madctl(uint8_t v)                    /* one parameter byte: always in 8-bit frames */
{
    frames(8);
    cmd8(0x36);
    put8(v);
}

static void range8(uint8_t cmd, uint16_t a, uint16_t b)
{
    cmd8(cmd);
    put8((uint8_t)(a >> 8));
    put8((uint8_t)a);
    put8((uint8_t)(b >> 8));
    put8((uint8_t)b);
}

/* classic (MADCTL 0): logical X0..X1, Y0..Y1 -> columns Y, rows 319 - X */
static void fill8(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    range8(0x2A, y0, y1);
    range8(0x2B, (uint16_t)(319u - x1), (uint16_t)(319u - x0));
    cmd8(0x2C);
    for (uint32_t n = (uint32_t)(x1 - x0 + 1u) * (uint32_t)(y1 - y0 + 1u); n; n--) {
        put8((uint8_t)(color >> 8));
        put8((uint8_t)color);
    }
}

/* nx (MADCTL 0xA0, 16-bit frames): columns = X, rows = Y */
static void fill16(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    cmd16(0x2A);
    put16(x0);
    put16(x1);
    cmd16(0x2B);
    put16(y0);
    put16(y1);
    cmd16(0x2C);
    for (uint32_t n = (uint32_t)(x1 - x0 + 1u) * (uint32_t)(y1 - y0 + 1u); n; n--)
        put16(color);
}

static void corners(fill_fn fill, int on)
{
    fill(0, 0, 7, 7, on ? 0xF800u : 0);         /* red: top left */
    fill(312, 0, 319, 7, on ? 0x07E0u : 0);      /* green: top right */
    fill(0, 232, 7, 239, on ? 0x001Fu : 0);      /* blue: bottom left */
}

int main(void)
{
    REG(RCC_IOPENR) |= (1u << 0) | (1u << 1) | (1u << 2);   /* GPIOA, B, C */
    REG(RCC_APBENR1) |= 1u << 14;                           /* SPI2 */
    REG(RCC_APBENR2) |= 1u << 18;                           /* TIM17 */
    REG(GPIOB_BSRR) = 1u << 12;                             /* backlight fully on */
    REG(GPIOA_BSRR) = 1u << 24;                             /* LCD CS (PA8) low */
    REG(SYST_RVR) = 63999u;                                 /* 1 kHz */
    REG(SYST_CVR) = 0;
    REG(SYST_CSR) = 7u;
    frames(8);
    cmd8(0x11);                                  /* sleep out */
    cmd8(0x29);                                  /* display on */
    cmd8(0x21);                                  /* inversion on (panel variant A) */

    madctl(0x00);                                /* 1: the classic way */
    corners(fill8, 1);
    wait_ms(20);                                 /* the backlight has been on for a whole window */
    idle();
    st_shot(1, "classic");
    corners(fill8, 0);

    madctl(0xA0);                                /* 2: the nx way */
    frames(16);
    corners(fill16, 1);
    idle();
    st_shot(2, "nx");

    cmd16(0x33);                                 /* 3: scroll area = all 320 rows, start row 8 */
    put16(0);
    put16(320);
    put16(0);
    cmd16(0x37);
    put16(8);
    idle();
    st_shot(3, "scroll8");

    cmd16(0x13);                                 /* 4: normal display mode ends the scrolling */
    idle();
    st_shot(4, "noron");

    cmd16(0x10);                                 /* 5: sleep in: black */
    idle();
    st_shot(5, "sleep");

    cmd16(0x11);                                 /* 6: sleep out, BGR order */
    madctl(0xA8);
    idle();
    st_shot(6, "bgr");

    madctl(0xA0);                                /* 7: backlight at 25 % */
    REG(TIM17 + 0x28u) = 63u;                    /* 1 MHz */
    REG(TIM17 + 0x2Cu) = 749u;
    REG(TIM17 + 0x14u) = 1u;
    REG(TIM17 + 0x10u) = 0;
    REG(TIM17 + 0x0Cu) = 1u;
    REG(GPIOB_BSRR) = 1u << 28;                  /* off; the first update switches it on */
    REG(NVIC_ISER) = 1u << 22;
    REG(TIM17) = 1u;
    wait_ms(30);
    st_shot(7, "bl25");
    res[15] = DONE;
    return 0;
}
