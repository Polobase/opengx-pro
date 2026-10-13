#include "state.h"

const int16_t k_cal_defaults[32] = {
    32005, 1662, 316, 496, 1050, 638, 1662, 316, 496, 1000, 638, 1680, 316, 496, 1250, 638,
    1680, 316, 496, 1120, 638, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};

struct channel g_ch[2];

volatile uint16_t g_ui_flags = UIF_PC_UNLOCK;
volatile uint16_t g_pc_flags;
volatile uint16_t g_misc_flags;
volatile uint16_t g_factory_cmd_flags;
volatile uint8_t g_ui_state = ST_BOOT;
uint8_t g_cal_index;
uint8_t g_cal_freq_exp;
uint8_t g_cal_ampl_exp;
int16_t g_cal_table[32] = {
    32005, 1662, 316, 496, 1050, 638, 1662, 316, 496, 1000, 638, 1680, 316, 496, 1250, 638,
    1680, 316, 496, 1120, 638, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};
char g_names[NAME_SLOTS][NAME_LEN];
