/* nx firmware (make APP=nx; docs/nx/fw2-design.md). The shared protocol core answers the PC
 * exactly like the classic firmware; the nx platform adds the SysTick/TIM6 time base, the FIFO
 * UART driver, the gesture engine, the TIM17 backlight, the watchdog and crash record, the
 * non-blocking generator queue and runner, and storage v2. The UI (nx_ui.c, the nx_scr_*.c
 * screens) is drawn by the UI engine (nx_lcd, nx_render); nx_resume.c offers what played before a
 * power cut. Builds without the vendor image. Boot order as the original's main and app_run
 * (docs/spec/application.md §2). */
#include "board.h"
#include "lcd.h"
#include "link.h"
#include "nx_bl.h"
#include "nx_core.h"
#include "nx_crc.h"
#include "nx_diag.h"
#include "nx_chain.h"
#include "nx_bfb.h"
#include "nx_manual.h"
#include "nx_terrain.h"
#include "nx_fault.h"
#include "nx_hist.h"
#include "nx_lcd.h"
#include "nx_loop.h"
#include "nx_resume.h"
#include "nx_settings.h"
#include "nx_store.h"
#include "nx_time.h"
#include "nx_ui.h"
#include "nx_wdg.h"
#include "spiflash.h"
#include "state.h"
#include "storage.h"
#include "uart.h"

/* app_run: both channels start with the empty program {0, 0, 120, 1, 1}, whose idle
 * configuration goes to the generators at the boot gate. */
static void default_programs(void)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        struct program *p = &g_ch[ch].prog;
        for (unsigned i = 0; i < PROGRAM_STEPS; i++)
            p->freq[i] = 0;
        p->hdr[HDR_WAVE] = 0;
        p->hdr[HDR_AMPL] = 0;
        p->hdr[HDR_OFFS] = 120;
        p->hdr[HDR_DWELL] = 1;
        p->hdr[HDR_COUNT] = 1;
    }
}

int main(void)
{
    nx_fault_init();   /* the reset cause first: RCC_CSR, then RMVF */
    clock_init();
    board_init();
    nx_time_init();
    nx_bl_init();
    uart_init();
    link_init();
    spiflash_init();
    storage_init();
    names_load();
    cal_load();
    default_programs();
    __enable_irq();

    delay_loop(0x82355);   /* as the original before lcd_init */
    g_nx_diag.panel = lcd_variant_name(lcd_init());   /* unchanged variant A/B lists, 8-bit */
    nx_lcd_init();         /* landscape, 16-bit frames, DMA: the UI engine owns SPI2 from here */
    nx_wdg_start();
    g_nx_diag.crc_hw = (uint8_t)nx_crc_init();
    nx_store_mount();      /* reads only; an empty region is formatted on the first write */
    nx_settings_load(&g_nx_settings);
    nx_hist_boot();        /* the history ring's head and the totals */
    nx_chain_boot();
    nx_terrain_boot();
    nx_manual_boot();
    nx_bfb_boot();
    nx_resume_boot();      /* a snapshot left by a power cut becomes the offer (and a history record) */
    nx_ui_init();
    nx_core_boot();        /* G1+G2 held: calibration menu */
    nx_run();
}
