/* Calibration-menu key handling shared by the classic and nx firmware (core_calmenu.h). */
#include "core_calmenu.h"

#include "link.h"
#include "state.h"
#include "storage.h"

int core_calmenu_key(enum key_code key, int repeat)
{
    unsigned i = g_cal_index;
    /* 3rd and 4th value of each group of five count the other way round */
    if (i >= 1u && i <= 20u && ((i - 1u) % 5u == 2u || (i - 1u) % 5u == 3u))
        flags_set(&g_misc_flags, MISC_CAL_INVERT);
    else
        flags_clear(&g_misc_flags, MISC_CAL_INVERT);
    int step = (g_misc_flags & MISC_CAL_INVERT) ? -1 : 1;

    if (key == KEY_G1 || key == KEY_G2) {   /* the only keys that auto-repeat */
        int dir = key == KEY_G1 ? step : -step;
        g_cal_table[i] = (int16_t)(g_cal_table[i] + dir);
        link_send_cal_value(i, (uint16_t)g_cal_table[i]);
        return 1;
    }
    if (repeat)
        return 0;
    switch (key) {
    case KEY_DOWN:
        g_cal_index = g_cal_index == 28u ? 0 : (uint8_t)(g_cal_index + 1u);
        cal_apply_test();
        return 1;
    case KEY_UP:
        g_cal_index = g_cal_index == 0 ? 28u : (uint8_t)(g_cal_index - 1u);
        cal_apply_test();
        return 1;
    case KEY_START:
        if (i == 26u) {
            cal_save(1);
            link_send_line(0, ":w61=1114111,");
        } else {
            cal_save(0);
            link_send_line(0, ":w61=1113111,");
        }
        return 0;
    case KEY_LOOP:
        g_cal_ampl_exp = (g_cal_ampl_exp == 0 || g_cal_ampl_exp == 10u) ? 4u : (uint8_t)(g_cal_ampl_exp + 1u);
        cal_apply_test();
        return 0;
    case KEY_STOP:
        g_cal_freq_exp = (g_cal_freq_exp == 0 || g_cal_freq_exp == 9u) ? 1u : (uint8_t)(g_cal_freq_exp + 1u);
        cal_apply_test();
        return 0;
    default:
        return 0;
    }
}
