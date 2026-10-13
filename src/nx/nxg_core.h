/* The gallery's core (APP=nxgallery and the host renderers): its virtual clock and its shots,
 * and nx_uicore.h answered from a fixture instead of the firmware, so every screen renders
 * without links, runner or SPI flash. src/nx/nxg_screens.c fills g_nxg per shot. */
#pragma once

#include <stdint.h>

#include "nx_uicore.h"

extern volatile uint32_t g_nxg_ms;   /* the gallery's clock; nx_ms() returns it */
extern unsigned g_nxg_shots;          /* shots taken so far: the id of the next one */

/* The glass is complete: the emulator saves a screenshot named after `name` (the second argument,
 * r1), the host renderer writes <name>.ppm. Provided by the platform (src/nx/nxg_stub.c, the host
 * main). */
void nxg_shot(unsigned id, const char *name);
/* Draws and sends everything pending; during a push only the transfers (its steps are due by
 * time). */
void nxg_settle(void);
/* nxg_settle, then nxg_shot(g_nxg_shots++, name). */
void nxg_take(const char *name);

struct nxg_fix {
    struct nx_vm vm;                   /* what nx_uc_vm returns */
    const char *names[32];             /* slot names (NULL: none) */
    struct nx_slot_info slots[30];     /* headers of slots 1..30 */
    char panel;                        /* what nx_uc_panel returns */
    int16_t cal[29];
    uint8_t blob[32];                  /* the UI settings blob */
    unsigned acts;                     /* nx_uc_act calls */
    uint8_t resume_offer;              /* what nx_uc_resume_offer returns */
    uint8_t resume_on;                 /* the resume setting */
    uint8_t resume_auto;               /* the offer is the auto-start */
    uint8_t presets;                   /* built-in presets with sweeps (0: none, as in most shots) */
};

extern struct nxg_fix g_nxg;

/* A fixed scene: G1 runs slot 2 at step 12 of 180, G2 paused on slot 7, programs in most
 * slots, panel variant A, dark theme, splash off. */
void nxg_fix_defaults(void);
