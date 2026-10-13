/* nx core: generator actions for the UI, the boot gate, PC-mode entry/exit, the calibration
 * boot and menu keys, and the power-off sequence (docs/nx/fw2-design.md, "Compatibility
 * contract"; docs/nx/design/core.md §2.3). The main loop (nx_loop.c) calls these in the order of
 * the original main loop.
 *
 * Actions change only the state the runner reads (g_ch[], RUN/LOOP flags), with the same effect
 * as the original's keys (app.c), so nx_runner sends what the original would send. They are
 * refused in PC mode (the PC owns the generators), in calibration mode and before the boot gate.
 *
 * NXA_LOAD      id = slot 1..30 into ch (the original's list START: both links resend), or
 *               another source (nx_src.h) with v = its amplitude in 0.01 V (0: its own)
 * NXA_PLAY      v = 0: run, loop off; v = 1: run looped (START / LOOP on a stopped channel)
 * NXA_PAUSE     v = 0: pause, loop off; v = 1: pause, loop on (START / LOOP while running)
 * NXA_STOP      stop ch: back to step 1, outputs off (STOP)
 * NXA_STOP_ALL  stop both, loop flags kept (the original's short POWER press)
 * NXA_LOOP      v = loop flag of ch, nothing is sent (nx: loop toggle while running)
 * NXA_SEEK      v = elapsed ms of ch's loaded program (resume after a power cut, nx_resume.h)
 * NXA_POWER_OFF request the power-off sequence */
#pragma once

#include <stdint.h>

#include "nx_keys.h"

enum nx_action_type {
    NXA_LOAD = 1,
    NXA_PLAY,
    NXA_PAUSE,
    NXA_STOP,
    NXA_STOP_ALL,
    NXA_LOOP,
    NXA_SEEK,
    NXA_POWER_OFF,
};

struct nx_action {
    uint8_t type;   /* enum nx_action_type */
    uint8_t ch;     /* 0 = G1, 1 = G2 */
    uint16_t id;    /* NXA_LOAD: slot, or a source of nx_src.h */
    uint32_t v;     /* NXA_PLAY / NXA_PAUSE / NXA_LOOP: see above */
    const void *p;  /* reserved for the later program sources (chains, presets) */
};

void nx_core_reset(void);
int nx_core_act(const struct nx_action *a);   /* NX_OK, NX_REFUSED_PC, NX_BUSY, NX_EINVAL */

/* Boot gate: nothing is sent or answered before NX_GEN_READY_MS, a factory-port command or the
 * calibration boot (the original's splash loop). Latches. */
void nx_core_update_ready(uint32_t now_ms);
int nx_core_ready(void);

/* PC mode: on a debounced PB1 change, once both queues are idle, runs the shared blocking
 * sequence (core_pcmode_update); until then the runner plans nothing new. Then ST_BOOT becomes
 * ST_HOME, as in the original's first pass (and after a factory ':a1'). */
void nx_core_pcmode_task(void);
int nx_core_runner_held(void);

/* Calibration boot (G1+G2 held at power-on, app.c): call once after lcd_init. Returns 1 when it
 * ran; the device then stays in ST_CAL (until power-off or a factory ':a1'). */
int nx_core_boot(void);
/* A key event in ST_CAL: PRESS and REPEAT of a single key go to core_calmenu_key. Returns 1
 * when the value grid needs a redraw. */
int nx_core_cal_key(const struct nx_kev *e);

/* Power-off: POWER held NX_KEY_POWER_OFF_MS (nx_keys) or NXA_POWER_OFF. The sequence shows the
 * goodbye screen for NX_GOODBYE_MS, then sends the PB2 power-off signal; no link or runner work
 * happens after the request (the original is blocked in its goodbye delay). */
int nx_core_power_off_due(void);
__attribute__((noreturn)) void nx_core_power_off(void);
