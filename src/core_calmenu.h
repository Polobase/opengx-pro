/* Calibration menu (G1+G2 held at power-on; docs/spec/application.md §9.1): key handling shared
 * by the classic and the nx firmware, so the generator lines (:w60 values, :w61 save, test
 * signals) stay the original's. */
#pragma once

#include "keys.h"

/* Call once per main-loop pass in ST_CAL with the pressed key (KEY_NONE if none) and whether it
 * is an auto-repeat of a held key; only G1/G2 act on repeats. Updates the direction flag
 * (MISC_CAL_INVERT) every call. Returns 1 when the value grid needs a redraw. */
int core_calmenu_key(enum key_code key, int repeat);
