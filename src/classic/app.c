/* Application core: TIM3 tick logic, key events, state machine, PC mode, calibration menu,
 * power off and the main loop (docs/spec/application.md §1, §3, §5, §8, §9.1). */
#include "app.h"

#include "board.h"
#include "core_calmenu.h"
#include "core_pcmode.h"
#include "keys.h"
#include "lcd.h"
#include "link.h"
#include "state.h"
#include "storage.h"
#include "ui.h"

/* Same delay_loop count as the shared core's PC-mode key scan (core_pcmode.h); aliased so the
 * two cannot drift apart. */
#define KEY_DEBOUNCE CORE_DELAY_13MS

/* ---- TIM3 tick (original TIM3_IRQHandler 0x0800C604, the non-PWM part) ---- */

void tick_isr_hook(void)
{
    g_elapsed_prescaler = g_elapsed_prescaler < 100u ? (uint8_t)(g_elapsed_prescaler + 1u) : 0;
    if (g_elapsed_prescaler == 50u) {          /* once per 101 ticks = 10.1 ms */
        if (g_ui_flags & UIF_RUN_G1)
            g_ch[0].elapsed++;
        if (g_ui_flags & UIF_RUN_G2)
            g_ch[1].elapsed++;
    }

    if (g_key_event < 0x11u)
        g_key_hold_count = 0;
    else if (g_tick8 == 0 && g_key_hold_count != 0xFFu)
        g_key_hold_count++;                    /* +1 per 25.6 ms while a key is held */

    if (g_tick8 == 100u) {                     /* marquee: one column per 25.6 ms */
        if (!(g_ui_flags & UIF_MARQUEE)) {
            g_marquee_offset = 0;
        } else if (g_marquee_offset < g_name_bmp_width + 130u) {
            g_marquee_offset++;
        } else {
            g_marquee_offset = 100;
            g_ui_flags ^= UIF_PC_NAME_G2;
        }
    }
}

/* ---- keys (§3) ---- */

/* The original's scan always runs its debounce delay first; this also paces the main loop
 * exactly like the original (13.3 ms per scan). */
static enum key_code key_scan(void)
{
    delay_loop(KEY_DEBOUNCE);
    return keys_read();
}

static void key_event_update(void)
{
    enum key_code k = key_scan();
    if (k == KEY_NONE) {
        if (g_key_event != 0) {
            if (g_key_event < 0x20u)
                g_key_event |= g_key_hold_count < 16u ? 0x20u : 0x40u;   /* release event */
            else
                g_key_event = 0;
        }
    } else if (g_key_event == 0) {
        g_key_event = (uint8_t)key_scan();     /* second scan: press event */
    } else {
        g_key_event |= KEV_HELD;
    }
}

static int pressed(unsigned code)
{
    return g_key_event == code;
}

/* Auto-repeat of a held key in the list (§3.2): hold > 0.28 s, first 10 ms of each 25.6 ms window. */
static int list_repeat(unsigned code)
{
    return g_key_event == (code | KEV_HELD) && g_key_hold_count > 10u && g_tick8 < 100u;
}

static int cal_repeat(unsigned code)
{
    return g_key_event == (code | KEV_HELD) && g_key_hold_count > 9u && g_tick8 > 99u;
}

/* ---- state machine (§5.1) ---- */

static void redraw(void)
{
    flags_set(&g_ui_flags, UIF_REDRAW);
}

static void goto_list(unsigned ch)
{
    g_ui_state = ST_LIST;
    if (ch)
        flags_set(&g_ui_flags, UIF_LIST_G2);
    else
        flags_clear(&g_ui_flags, UIF_LIST_G2);
    redraw();
}

static int pc_mode_update(void)   /* non-zero: PC mode was entered or left in this pass */
{
    return core_pcmode_update(pin_read(GPIOB, HOST_DETECT_PIN)) != PCM_NONE;
}

static void list_keys(void)
{
    unsigned target = (g_ui_flags & UIF_LIST_G2) ? 1u : 0u;
    if (pressed(KEY_START)) {
        struct channel *c = &g_ch[target];
        g_ui_state = target ? ST_CH2 : ST_CH1;
        g_ch[0].step_sent = g_ch[1].step_sent = 0xFF;
        flags_clear(&g_ui_flags, UIF_RUN(target));
        c->elapsed = 0;
        program_load(g_list_sel, target);
        c->prog_slot = g_list_sel;
        redraw();
    } else if (pressed(KEY_STOP)) {
        g_ui_state = target ? ST_CH2 : ST_CH1;
        redraw();
    } else if (pressed(KEY_G1)) {
        goto_list(0);
    } else if (pressed(KEY_G2)) {
        goto_list(1);
    } else if (pressed(KEY_UP) || list_repeat(KEY_UP)) {
        if (g_list_sel != 1u)
            g_list_sel--;
        if (g_list_sel < g_list_top)
            g_list_top = g_list_sel;
        flags_set(&g_ui_flags, UIF_LIST_UP | UIF_REDRAW);
    } else if (pressed(KEY_DOWN) || list_repeat(KEY_DOWN)) {
        if (g_list_sel != PROGRAM_SLOTS)
            g_list_sel++;
        if (g_list_sel > g_list_top + 6u)
            g_list_top++;
        flags_clear(&g_ui_flags, UIF_LIST_UP);
        redraw();
    }
}

/* START / STOP / LOOP / POWER on a channel screen (CH1: ch 0, CH2: ch 1). */
static void channel_keys(unsigned ch)
{
    struct channel *c = &g_ch[ch];
    unsigned other = ch ^ 1u;
    if (pressed(ch ? KEY_G2 : KEY_G1)) {
        goto_list(ch);
    } else if (pressed(ch ? KEY_G1 : KEY_G2)) {
        if (g_ch[other].prog_slot) {
            g_ui_state = other ? ST_CH2 : ST_CH1;
            redraw();
        } else {
            goto_list(other);
        }
    } else if (pressed(KEY_START)) {
        c->step_sent = 0xFF;
        if (!(g_ui_flags & UIF_RUN(ch))) {
            flags_set(&g_ui_flags, UIF_RUN(ch));
            flags_clear(&g_ui_flags, UIF_LOOP(ch));
        } else {
            flags_clear(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        }
        redraw();
    } else if (pressed(KEY_STOP)) {
        c->step_sent = 0xFF;
        c->elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        redraw();
    } else if (pressed(KEY_POWER)) {          /* short press: stop both channels */
        g_ch[0].step_sent = g_ch[1].step_sent = 0xFF;
        g_ch[0].elapsed = g_ch[1].elapsed = 1;
        flags_clear(&g_ui_flags, UIF_RUN_G1 | UIF_RUN_G2);
        redraw();
    } else if (pressed(KEY_LOOP)) {
        c->step_sent = 0xFF;
        if (!(g_ui_flags & UIF_RUN(ch))) {
            flags_set(&g_ui_flags, UIF_RUN(ch) | UIF_LOOP(ch));
        } else {
            flags_clear(&g_ui_flags, UIF_RUN(ch));
            flags_set(&g_ui_flags, UIF_LOOP(ch));
        }
        redraw();
    }
}

/* The classic key event (press = code, held = code | KEV_HELD) as the core's (key, repeat). */
static void cal_keys(void)
{
    enum key_code k = KEY_NONE;
    int repeat = 0;
    if (g_key_event != 0 && g_key_event < KEV_HELD) {
        k = (enum key_code)g_key_event;
    } else if ((g_key_event & 0xF0u) == KEV_HELD && cal_repeat(g_key_event & 0x0Fu)) {
        k = (enum key_code)(g_key_event & 0x0Fu);
        repeat = 1;
    }
    if (core_calmenu_key(k, repeat))
        redraw();
}

static void dispatch_keys(void)
{
    switch (g_ui_state) {
    case ST_BOOT:
        g_ui_state = ST_HOME;
        redraw();
        break;
    case ST_HOME:
        if (pressed(KEY_G1))
            goto_list(0);
        else if (pressed(KEY_G2))
            goto_list(1);
        break;
    case ST_LIST:
        list_keys();
        break;
    case ST_CH1:
        channel_keys(0);
        break;
    case ST_CH2:
        channel_keys(1);
        break;
    case ST_CAL:
        cal_keys();
        break;
    default:   /* PC mode: no key handling */
        break;
    }
}

/* ---- power off (§8) ---- */

static void power_off(void)
{
    g_backlight_duty = 0;
    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    ui_draw_shutdown_screen();
    g_backlight_duty = 8;
    delay_loop(64000000u);   /* ~4 s, then the power-off signal (verified on the device) */
    power_off_signal();
}

/* ---- boot and main loop (§2, §5) ---- */

void app_run(void)
{
    g_list_top = g_list_sel = 1;
    names_load();
    for (unsigned ch = 0; ch < 2; ch++) {
        struct program *p = &g_ch[ch].prog;
        p->hdr[HDR_WAVE] = 0;
        p->hdr[HDR_AMPL] = 0;
        p->hdr[HDR_OFFS] = 120;
        p->hdr[HDR_DWELL] = 1;
        p->hdr[HDR_COUNT] = 1;
        for (unsigned i = 0; i < PROGRAM_STEPS; i++)
            p->freq[i] = 0;
    }
    cal_load();
    g_pb7_duty = 1;

    lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
    g_backlight_duty = 8;
    ui_draw_boot_splash();

    if (key_scan() == KEY_G1_G2) {             /* G1+G2 held at power-on: calibration menu */
        g_ui_state = ST_CAL;
        while (key_scan() != KEY_NONE) {
        }
        redraw();
        link_send_line(0, ":r80=0,");
        uint32_t sent = g_ticks;
        lcd_fill_rect(0, 0, LCD_W - 1u, LCD_H - 1u, COLOR_BLACK);
        /* DEVIATION: the original checks once, right after its (slower) screen clear, whether
         * G1's ":w80=..." reply with the calibration values has arrived. Our clear is faster, so
         * wait for the reply up to the same point in time (~110 ms after ":r80=0,"). */
        while (!(g_ui_flags & UIF_LINE_G1) && g_ticks - sent < 1100u) {
        }
        if (g_ui_flags & UIF_LINE_G1)
            link_process(0);
        ui_cal_draw_values();
    } else {                                    /* splash for ~4 s, or until a factory command */
        for (unsigned i = 0; i < 100u && !(g_factory_cmd_flags & 1u); i++)
            delay_loop(640000u);
    }
    if (g_ui_state == ST_CAL) {
        pin_high(GPIOB, HANDSHAKE_PIN);
        cal_apply_test();
    }
    /* DEVIATION: the original's first-boot SPI-flash wipe (flag in the bootloader page), its
     * licence check and its forced read-out protection are not reimplemented. */

    for (;;) {
        key_event_update();
        if (g_key_event == (KEY_POWER | KEV_HELD) && g_key_hold_count > 30u)
            power_off();
        /* The original leaves out the key dispatch of a pass that enters or leaves PC mode
         * (0x0800E746 -> 0x0800F182): a key first seen in that pass is not acted on. */
        if (!pc_mode_update())
            dispatch_keys();
        if (g_ui_state != ST_PC && g_ui_state != ST_CAL)
            runner_tick();
        if (g_ui_flags & UIF_LINE_G1)
            link_process(0);
        if (g_ui_flags & UIF_LINE_G2)
            link_process(1);
        ui_render();
        if (g_factory_cmd_flags & 1u)
            link_factory_execute();
    }
}
