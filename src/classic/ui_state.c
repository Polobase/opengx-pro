/* State of the classic UI and its key handling (app.c, ui.c; docs/spec/application.md §4).
 * The nx firmware does not link this file. */
#include "state.h"

uint8_t g_ui_prev_state;
volatile uint8_t g_key_event;
volatile uint8_t g_key_hold_count;
volatile uint8_t g_elapsed_prescaler;
volatile uint16_t g_marquee_offset;
uint16_t g_marquee_prev_offset;
uint8_t g_list_top;
uint8_t g_list_sel;
uint16_t g_name_bmp_width;
uint8_t g_name_bmp[1998];
