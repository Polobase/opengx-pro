/* Image-size spike (docs/nx/drop0-checklist.md): the vendor bootloader has only been seen
 * accepting the original's 74,672-byte image. Flashed as a padded image
 * (make APP=sizeprobe PAD_KB=<n> flash-padded; tools/pad_image.py appends a filler and a trailer
 * {"NXSZ", length, CRC-32, ~CRC-32}), this app shows on the device:
 *   - whether the CRC over the whole image matches the trailer (every block arrived intact)
 *   - whether the flash behind the image is erased (how much the bootloader erases)
 *   - the reset flags (RCC_CSR) as the application sees them after the bootloader
 *   - which RAM areas kept their content across a reset through the bootloader (for the nx
 *     crash record): patterns are written at every boot and checked at the next one
 * Keys: START = software reset, LOOP = watchdog reset (IWDG, ~2 s), POWER held = power off.
 * Nothing is sent on the serial links; neither the SPI flash nor the MCU flash is written. */
#include <stdint.h>

#include "board.h"
#include "fmt.h"
#include "keys.h"
#include "lcd.h"

#define APP_START     0x08004000u
#define FLASH_END     0x08020000u
#define TRAILER_MAGIC 0x5A53584Eu   /* "NXSZ" read as little-endian u32 */
#define COLOR_GREEN   0x07E0u
#define LINE_Y(n)     (6u + (n) * 28u)

extern uint32_t _sidata, _sdata, _edata, _ebss;

struct trailer {
    uint32_t magic, length, crc, ncrc;
};

/* RAM probe areas: above this app's .data/.bss (checked at run time) and below its stack
 * (top of RAM). */
static const uint32_t k_probe_addr[4] = { 0x20002000u, 0x20004000u, 0x20006000u, 0x20007000u };
#define PROBE_WORDS 16u

static uint32_t probe_word(unsigned area, unsigned i)
{
    return 0x5EED0000u ^ (area << 8) ^ (i * 0x9E3779B9u);
}

/* Bit n set = area n still held the pattern of the previous boot. Then writes fresh patterns. */
static unsigned ram_probe(void)
{
    unsigned kept = 0;
    for (unsigned a = 0; a < 4u; a++) {
        volatile uint32_t *p = (volatile uint32_t *)k_probe_addr[a];
        unsigned ok = 1;
        for (unsigned i = 0; i < PROBE_WORDS; i++)
            if (p[i] != probe_word(a, i))
                ok = 0;
        if (ok)
            kept |= 1u << a;
        for (unsigned i = 0; i < PROBE_WORDS; i++)
            p[i] = probe_word(a, i);
    }
    return kept;
}

/* The trailer written by pad_image.py: 16-byte aligned, behind this app's own image. */
static const struct trailer *find_trailer(void)
{
    uint32_t end = (uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata);
    for (uint32_t a = (end + 15u) & ~15u; a + 16u <= FLASH_END; a += 16u) {
        const struct trailer *t = (const struct trailer *)a;
        if (t->magic == TRAILER_MAGIC && t->length == a + 16u - APP_START && t->ncrc == ~t->crc)
            return t;
    }
    return 0;
}

static void line(unsigned n, const char *text, uint16_t color)
{
    gfx_fill(0, (uint16_t)LINE_Y(n), 319, (uint16_t)(LINE_Y(n) + 23u), COLOR_BLACK);
    gfx_text(6, (uint16_t)LINE_Y(n), text, color, COLOR_BLACK);
}

static void show_image(void)
{
    char buf[48];
    const struct trailer *t = find_trailer();
    if (!t) {
        line(0, "SIZE PROBE  (not padded)", COLOR_WHITE);
        line(1, "no trailer: flash a padded image", COLOR_GREY);
        return;
    }
    char *p = fmt_str(buf, "SIZE PROBE  ");
    p = fmt_uint(p, t->length / 1024u);
    fmt_str(p, " KB image");
    line(0, buf, COLOR_WHITE);

    uint32_t crc = fmt_crc32(0, (const uint8_t *)APP_START, (uint32_t)t - APP_START);
    line(1, crc == t->crc ? "CRC OK: every byte arrived" : "CRC FAIL: image damaged", crc == t->crc ? COLOR_GREEN : COLOR_RED);

    uint32_t a = (uint32_t)t + 16u;
    while (a < FLASH_END && *(const volatile uint32_t *)a == 0xFFFFFFFFu)
        a += 4u;
    if (a >= FLASH_END) {
        line(2, "flash behind image: erased", COLOR_WHITE);
    } else {
        p = fmt_str(buf, "behind image: data at 0x");
        fmt_hex(p, a, 8);
        line(2, buf, COLOR_YELLOW);
    }
}

static void show_reset(uint32_t csr, unsigned kept)
{
    static const struct { uint32_t bit; const char *name; } k_flags[] = {
        { RCC_CSR_LPWRRSTF, " LPWR" }, { RCC_CSR_WWDGRSTF, " WWDG" }, { RCC_CSR_IWDGRSTF, " IWDG" },
        { RCC_CSR_SFTRSTF, " SFT" }, { RCC_CSR_PWRRSTF, " PWR" }, { RCC_CSR_PINRSTF, " PIN" },
        { RCC_CSR_OBLRSTF, " OBL" },
    };
    char buf[48], *p = fmt_str(buf, "reset:");
    for (unsigned i = 0; i < sizeof k_flags / sizeof k_flags[0]; i++)
        if (csr & k_flags[i].bit)
            p = fmt_str(p, k_flags[i].name);
    if (p == buf + 6)
        fmt_str(p, " (no flags)");
    line(3, buf, COLOR_WHITE);

    p = fmt_str(buf, "RAM kept:");
    for (unsigned n = 0; n < 4u; n++) {
        if (kept & (1u << n)) {
            p = fmt_str(p, " ");
            p = fmt_hex(p, k_probe_addr[n] & 0xFFFFu, 4);
        }
    }
    if (!kept)
        fmt_str(p, " none");
    line(4, buf, COLOR_WHITE);
    if ((uint32_t)&_ebss > k_probe_addr[0])
        line(5, "WARNING: .bss overlaps RAM probe", COLOR_RED);
}

static __attribute__((noreturn)) void watchdog_reset(void)
{
    line(7, "watchdog armed: reset in ~2 s", COLOR_YELLOW);
    IWDG->KR = 0xCCCCu;   /* start (also starts the LSI) */
    IWDG->KR = 0x5555u;   /* unlock PR and RLR */
    IWDG->PR = 4u;        /* LSI 32 kHz / 64 = 500 Hz */
    IWDG->RLR = 999u;     /* 1000 counts = 2 s */
    for (uint32_t n = 0; IWDG->SR && n < 1000000u; n++) {
    }
    IWDG->KR = 0xAAAAu;   /* reload with the new value, then never again */
    for (;;) {
    }
}

int main(void)
{
    uint32_t csr = RCC->CSR;   /* before anything else: flags as the bootloader left them */
    RCC->CSR |= RCC_CSR_RMVF;
    unsigned kept = ram_probe();

    clock_init();
    board_init();
    __enable_irq();
    delay_loop(0x82355);
    lcd_init();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    g_backlight_duty = 8;

    show_image();
    show_reset(csr, kept);
    line(6, "START reset  LOOP watchdog", COLOR_GREY);
    line(7, "POWER hold: power off", COLOR_GREY);

    enum key_code prev = KEY_NONE;
    uint32_t power_since = 0;
    for (;;) {
        uint32_t t = g_ticks;
        while (g_ticks - t < 100u) {   /* 10 ms */
        }
        enum key_code k = keys_read();
        if (k != prev && k == KEY_START)
            NVIC_SystemReset();
        if (k != prev && k == KEY_LOOP)
            watchdog_reset();
        if (k == KEY_POWER) {
            if (prev != KEY_POWER)
                power_since = g_ticks;
            else if (g_ticks - power_since > 8000u)
                power_off_signal();
        }
        prev = k;
    }
}
