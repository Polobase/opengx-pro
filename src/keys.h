/* Front-panel keys, active low with pull-ups (docs/spec/application.md §3.1). */
#pragma once

#include <stdint.h>

enum key_code {
    KEY_NONE = 0,
    KEY_LOOP = 1,        /* PD0 */
    KEY_START = 2,       /* PD1, start/pause/confirm */
    KEY_STOP = 3,        /* PB5 */
    KEY_UP = 4,          /* PB4 */
    KEY_DOWN = 5,        /* PD3 */
    KEY_G1 = 6,          /* PB3 */
    KEY_G2 = 7,          /* PD2 */
    KEY_G1_G2 = 8,       /* PB3 + PD2 */
    KEY_POWER = 9,       /* PB11 */
    KEY_POWER_STOP = 10, /* PB11 + PB5 */
};

/* Instantaneous scan, same priority order as the original keys_scan 0x0800E0F8 (no delay). */
enum key_code keys_read(void);
const char *key_name(enum key_code code);
