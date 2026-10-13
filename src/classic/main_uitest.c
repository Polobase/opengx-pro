/* UI test app (make APP=uitest): shows every screen of the original UI in turn, one per second,
 * by setting the shared state (state.h) and calling the UI module like the application does.
 * Screen n is complete before n*1000 + 900 ms, so the emulator can take screenshots:
 *
 *   .venv/bin/python tools/emu.py build/uitest/gxpro.elf --until 21000 \
 *       --shot 1900:build/emu/ui01_splash.png --shot 2900:build/emu/ui02_home_boot.png ...
 *
 * A test app for the emulator only: the firmware's screens are compared pixel by pixel with the
 * original by tools/diff.py. The TIM3 marquee logic is not part of this app, so the
 * marquee offset is set directly. Never transmits, never writes flash. POWER shows the goodbye
 * screen and sends the power-off signal like the original. */
#include <stddef.h>
#include <string.h>

#include "board.h"
#include "gfx.h"
#include "lcd.h"
#include "state.h"
#include "ui.h"

/* struct channel's layout, as tools that read it from RAM expect it (tools/pc_replay.py). */
_Static_assert(offsetof(struct channel, prog_slot) == 0x988, "struct channel layout");
_Static_assert(offsetof(struct channel, step_freq) == 0x990, "struct channel layout");
_Static_assert(offsetof(struct channel, elapsed) == 0x998, "struct channel layout");
_Static_assert(offsetof(struct channel, pc_freq) == 0x9A0, "struct channel layout");
_Static_assert(sizeof(struct channel) == 0x9B0, "struct channel layout");

static unsigned screen;

static void power_off(void)
{
    g_backlight_duty = 0;
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    ui_draw_shutdown_screen();
    g_backlight_duty = 8;
    delay_loop(64000000u);
    power_off_signal();
}

/* Screen n starts at n seconds. */
static void next_screen(void)
{
    screen++;
    while (g_ticks < screen * 10000u) {
        if (!pin_read(GPIOB, 11))   /* POWER */
            power_off();
    }
}

/* Hook points for a tool on the emulator (the calls can be followed by address). */
__attribute__((noinline)) void uitest_done(unsigned id)
{
    __asm volatile("" : : "r"(id) : "memory");
}

__attribute__((noinline)) void uitest_clear(void)
{
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
}

/* PC-link icon left over from PC mode, as in the reference mock-up of the CH1 screen. */
__attribute__((noinline)) void uitest_usb_remnant(void)
{
    gfx_usb_icon(4, 5, COLOR_WHITE, COLOR_BLACK);
}

static void set_name(unsigned slot, const char *s)
{
    memset(g_names[slot], 0, NAME_LEN);
    size_t n = strlen(s);
    memcpy(g_names[slot], s, n < NAME_LEN ? n : NAME_LEN);
}

static void redraw(uint8_t state, uint8_t prev)
{
    g_ui_state = state;
    g_ui_prev_state = prev;
    flags_set(&g_ui_flags, UIF_REDRAW);
    ui_render();
}

/* LIST moves as the application does them (application.md §5.1), each with a full render. */
static void list_down(unsigned n)
{
    while (n--) {
        if (g_list_sel != 30u)
            g_list_sel++;
        if (g_list_sel > g_list_top + 6u)
            g_list_top++;
        flags_clear(&g_ui_flags, UIF_LIST_UP);
        flags_set(&g_ui_flags, UIF_REDRAW);
        ui_render();
    }
}

static void list_up(unsigned n)
{
    while (n--) {
        if (g_list_sel != 1u)
            g_list_sel--;
        if (g_list_sel < g_list_top)
            g_list_top = g_list_sel;
        flags_set(&g_ui_flags, UIF_LIST_UP | UIF_REDRAW);
        ui_render();
    }
}

/* Incremental render only (marquee step, PC fields). */
static void step(uint16_t marquee_offset)
{
    g_marquee_offset = marquee_offset;
    ui_render();
}

static void load(unsigned ch, uint8_t slot, int running, uint32_t elapsed, uint64_t freq)
{
    g_ch[ch].prog_slot = slot;
    g_ch[ch].elapsed = elapsed;
    g_ch[ch].step_freq = freq;
    if (running)
        flags_set(&g_ui_flags, UIF_RUN(ch));
    else
        flags_clear(&g_ui_flags, UIF_RUN(ch));
}

static void loop_flags(int g1, int g2)
{
    flags_clear(&g_ui_flags, UIF_LOOP_G1 | UIF_LOOP_G2);
    flags_set(&g_ui_flags, (uint16_t)((g1 ? UIF_LOOP_G1 : 0u) | (g2 ? UIF_LOOP_G2 : 0u)));
}

static void setup_names(void)
{
    static const char *const names[] = {
        "PC name for G1",                                        /* 0: PC mode G1 */
        "Detox",
        "Lyme Disease",
        "Pain Relief (general) - long program name scrolls",
        "Sleep",
        "Energy",
        "Immune Boost",
        "Parasites",
        "Schumann Resonance (CAFL)",
        "Cold and Flu",
        "Candida",
        "Mitochondria Support and General Wellness Set",       /* 11: long */
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",   /* 12: 944 px, wider than the buffer */
        "Herpes Simplex",
        "",                                                      /* 14: empty */
        "Unknown <chars> | skipped",                             /* 15: '<', '>', '|' are not in the font */
    };
    for (unsigned i = 0; i < NAME_SLOTS; i++) {
        char buf[16] = "Program ";
        buf[8] = (char)('0' + i / 10u);
        buf[9] = (char)('0' + i % 10u);
        set_name(i, i < sizeof names / sizeof names[0] ? names[i] : buf);
    }
    set_name(29, "Example Program");
    set_name(31, "A very long program name sent from the PC for generator 2");
}

static void run_all(void)
{
    memset(g_ch, 0, sizeof g_ch);
    g_ui_flags = UIF_PC_UNLOCK;
    g_pc_flags = 0;
    g_misc_flags = 0;
    g_marquee_offset = 0;
    g_marquee_prev_offset = 0;
    memcpy(g_cal_table, k_cal_defaults, sizeof g_cal_table);
    setup_names();

    /* 1: boot splash */
    next_screen();
    uitest_clear();
    ui_draw_boot_splash();
    uitest_done(screen);

    /* 2: HOME drawn over the splash, as after power-up (the HOME render does not clear everything) */
    next_screen();
    redraw(ST_HOME, ST_BOOT);
    uitest_done(screen);

    /* 3: HOME on a clear screen (reference mock-up screen_mock_idle.png) */
    next_screen();
    uitest_clear();
    redraw(ST_HOME, ST_HOME);
    uitest_done(screen);

    /* 4: LIST for G1 entered from HOME, entries 1..7, entry 3 selected (screen_mock_list_example.png) */
    next_screen();
    uitest_clear();
    g_list_top = 1;
    g_list_sel = 3;
    flags_clear(&g_ui_flags, UIF_LIST_G2 | UIF_LIST_UP);
    redraw(ST_LIST, ST_HOME);
    uitest_done(screen);

    /* 5: DOWN 8 times (list scrolls: entries 5..11, 11 selected), then the long name scrolls */
    next_screen();
    list_down(8);
    step(180);
    uitest_done(screen);

    /* 6: G2 key (list for G2), DOWN 7 times, UP 6 times (entries 12..18, 12 selected: a name wider
     * than the bitmap buffer, scrolling) */
    next_screen();
    flags_set(&g_ui_flags, UIF_LIST_G2);
    redraw(ST_LIST, ST_LIST);
    list_down(7);
    list_up(6);
    step(400);
    uitest_done(screen);

    /* 7: CH1 like the reference mock-up screen_mock_state3_example.png: "Example Program" running at
     * 37 500 Hz, loop on, G2 without program, PC-link icon left over */
    next_screen();
    uitest_clear();
    uitest_usb_remnant();
    load(0, 29, 1, 500, 3750007u);
    load(1, 0, 0, 0, 0);
    loop_flags(1, 0);
    redraw(ST_CH1, ST_LIST);
    uitest_done(screen);

    /* 8: CH1 from LIST: G1 freshly loaded ('c', long name static), G2 paused ('p', loop on) */
    next_screen();
    load(0, 3, 0, 0, 0);
    load(1, 11, 0, 12345, 3750007u);
    loop_flags(0, 1);
    redraw(ST_CH1, ST_LIST);
    uitest_done(screen);

    /* 9: CH1 status update only: G1 stopped ('s'), G2 running at 7.83 Hz (leading-zero layout) */
    next_screen();
    load(0, 3, 0, 1, 0);
    load(1, 11, 1, 777, 7836u);
    redraw(ST_CH1, ST_CH1);
    uitest_done(screen);

    /* 10: CH1 marquee step: G1 name scrolled (offset 250) */
    next_screen();
    step(250);
    uitest_done(screen);

    /* 11: CH2 from CH1: G2 running at 1 234 560 Hz, G1 paused, both loops on */
    next_screen();
    load(0, 3, 0, 4242, 0);
    load(1, 11, 1, 99, 123456007u);
    loop_flags(1, 1);
    redraw(ST_CH2, ST_CH1);
    uitest_done(screen);

    /* 12: CH2 from LIST, G1 without program ("Choose ..."), G2 freshly loaded */
    next_screen();
    load(0, 0, 0, 0, 0);
    load(1, 8, 0, 0, 0);
    loop_flags(0, 0);
    redraw(ST_CH2, ST_LIST);
    uitest_done(screen);

    /* 13: CH1, both running: frequency layouts 1 Hz (G1) and 25 Hz (G2) */
    next_screen();
    load(0, 1, 1, 50, 1000000000u);
    load(1, 2, 1, 50, 250005u);
    redraw(ST_CH1, ST_CH2);
    uitest_done(screen);

    /* 14: CH2: G2 at 123456789 (shown as 23456780.00000000 Hz: the readout keeps 16 digits), G1 at 1
     * (value 0: "0.00000000 Hz") */
    next_screen();
    load(0, 1, 1, 50, 1u);
    load(1, 2, 1, 50, 123456789u);
    redraw(ST_CH2, ST_CH1);
    uitest_done(screen);

    /* 15: PC mode entered from CH2: names of slots 0 and 31, four frequencies (grey), G1 in sync */
    next_screen();
    load(0, 0, 0, 0, 0);
    load(1, 0, 0, 0, 0);
    flags_clear(&g_ui_flags, UIF_RUN_G1 | UIF_RUN_G2 | UIF_LOOP_G1 | UIF_LOOP_G2 | UIF_PC_NAME_G2);
    flags_set(&g_ui_flags, UIF_SYNC_G1);
    g_ch[0].pc_freq[0] = 3750007u;
    g_ch[0].pc_freq[1] = 7836u;
    g_ch[1].pc_freq[0] = 123456007u;
    g_ch[1].pc_freq[1] = 0;
    g_misc_flags = MISC_OUT_EN(0, 0) | MISC_OUT_EN(0, 1) | MISC_OUT_EN(1, 0);
    g_pc_flags = 0;
    redraw(ST_PC, ST_CH2);
    uitest_done(screen);

    /* 16: PC mode field updates: outputs on in teal, G1 out2 shows out1 (sync), G2 out2 grey */
    next_screen();
    g_ch[1].pc_freq[1] = 250005u;
    flags_clear(&g_pc_flags, 0x0F);
    step(0);
    uitest_done(screen);

    /* 17: PC mode, G2 name scrolling (offset 200), G1 name re-sent (drawn static) */
    next_screen();
    flags_set(&g_ui_flags, UIF_PC_NAME_G2);
    set_name(0, "New PC name for generator 1, also quite long");
    flags_clear(&g_pc_flags, PCF_NAME(0));
    step(200);
    uitest_done(screen);

    /* 18: HOME after PC mode (PC-link icon erased) */
    next_screen();
    redraw(ST_HOME, ST_PC);
    uitest_done(screen);

    /* 19: calibration grid, value 5 selected */
    next_screen();
    uitest_clear();
    g_cal_index = 5;
    ui_cal_draw_values();
    redraw(ST_CAL, ST_CAL);
    uitest_done(screen);

    /* 20: calibration: UP five times to value 0, factory commands set values 22 and 26 */
    next_screen();
    while (g_cal_index) {
        g_cal_index--;
        redraw(ST_CAL, ST_CAL);
    }
    g_cal_table[22] = 12345;
    ui_cal_draw_value(22);
    g_cal_table[26] = -1;   /* shown as 65535 */
    ui_cal_draw_value(26);
    uitest_done(screen);

    /* 21: goodbye screen */
    next_screen();
    uitest_clear();
    ui_draw_shutdown_screen();
    uitest_done(screen);
}

int main(void)
{
    clock_init();
    board_init();
    __enable_irq();

    delay_loop(0x82355);
    lcd_init();
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    g_backlight_duty = 8;

    for (;;)
        run_all();
}
