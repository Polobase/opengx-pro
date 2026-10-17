/* nx platform constants (docs/nx/fw2-design.md, "Core decisions"). The values marked "Drop 0"
 * are hardware facts measured by the device spikes; each has a safe default that is used until
 * docs/nx/drop0-checklist.md has been filled in. The Drop-0 values of
 * the linker script (NX_FLASH_LIMIT, NX_NOINIT_ADDR) are in ld/nx.ld. */
#pragma once

/* ---- Drop 0 values ---- */

/* First generator transmission and first reply to the PC, in ms after power-on: the original's
 * first main-loop pass (4441 ms in the emulator). Set from docs/nx/drop0-checklist.md results
 * (the generator test "T0 first :ok"): only lower it when the first :ok arrives well before this time. */
#define NX_GEN_READY_MS 4440u

/* :ok wait per generator line before it counts as a timeout, in ms: the original's gen_wait_ok.
 * Set from docs/nx/drop0-checklist.md results (the generator test "T1 ... latency min/avg/max"); keep 40 unless
 * the measured maximum latency needs more. */
#define NX_TXQ_ACK_TIMEOUT_MS 40u

/* 1: the generators answer display-originated lines with :ok in standalone mode, so a missing
 * :ok is a link error; 0: they do not (the txq still waits NX_TXQ_ACK_TIMEOUT_MS per line, like
 * the original, but timeouts are not reported as errors). Set from docs/nx/drop0-checklist.md
 * results (the generator test "T1 PB2=0 ok/err/timeout"). */
#define NX_GEN_ACKS 1

/* 1: the independent watchdog runs (nx_wdg.c, 2 s). Set from docs/nx/drop0-checklist.md results
 * (§1 "after the watchdog reset: Stop+Power still enters programming mode?"): 0 if it does not. */
#define NX_IWDG_ENABLE 1

/* Backlight PWM period in us (TIM17 two-phase PWM). Set from docs/nx/drop0-checklist.md results
 * (§2 page 5 "flicker or whine at 625 Hz / 1 kHz / 2 kHz"): 1000 = 1 kHz, 500 = 2 kHz. */
#define NX_BL_PERIOD_US 1000u

/* Panel addressing after the unchanged variant A/B init lists (nx_lcd.c): 0xA0 (MV|MY) makes
 * CASET the logical X axis and RASET the logical Y axis (landscape, as the classic firmware shows
 * it). Checked on the unit (docs/nx/drop0-checklist.md §2 page 1): 0x60 (MV|MX) shows the picture
 * turned 180 degrees. */
#ifndef NX_LCD_MADCTL
#define NX_LCD_MADCTL 0xA0u
#endif

/* 1: SPI2 sends 16-bit frames: commands as NOP-prefixed frames 0x00cc, parameters and pixels as
 * 16-bit values (high byte first, no byte swapping). 0: the 8-bit fallback: pixels are
 * byte-swapped (REV16) in memory and sent as packed 16-bit writes. Set from
 * docs/nx/drop0-checklist.md results (§2 page 2: the "16-bit polled" fill shows the right colours
 * and the gradient band is visible). */
#ifndef NX_LCD_DS16
#define NX_LCD_DS16 1
#endif

/* 1: pixel data is sent by DMA1 channel 1 while the CPU draws the next band; 0: polled (one band
 * per render slice). Set from docs/nx/drop0-checklist.md results (§2 page 2 "16-bit DMA" and "DMA
 * bands 8 px" measured, no TIMEOUT). */
#ifndef NX_LCD_DMA
#define NX_LCD_DMA 1
#endif

/* DMAMUX request line of SPI2_TX (RM0454, "DMAMUX: assignment of multiplexer inputs"). Set from
 * docs/nx/drop0-checklist.md results (§2 page 2: "16-bit DMA" without TIMEOUT confirms 19). */
#ifndef NX_LCD_DMAREQ
#define NX_LCD_DMAREQ 19u
#endif

/* Hardware-scroll direction: +1 when a growing VSCSAD moves the picture to the right on the glass
 * (the emulator's ST7789 model), -1 when it moves it to the left. Set from
 * docs/nx/drop0-checklist.md results (§2 page 3 "white stripe moves left or right?"). */
#ifndef NX_LCD_SCROLL_SIGN
#define NX_LCD_SCROLL_SIGN (+1)
#endif

/* Expected time of one full-screen update in us: 76800 pixels at 32 MHz are 38.4 ms of SPI plus
 * the band windows. The backlight dip waits at most twice this for its render. Set from
 * docs/nx/drop0-checklist.md results (§2 page 2 "DMA bands 8 px" time). */
#ifndef NX_LCD_FRAME_US
#define NX_LCD_FRAME_US 40000u
#endif

/* ---- other platform constants (design values, not measured) ---- */

#define NX_PB1_DEBOUNCE_MS 15u         /* PB1 (PC connected) must be stable this long */
#define NX_W91_HOLD_MS 15u             /* a :w91 (PB2 low) waits this long after a :w92 sequence (nx_loop.c): >= the original's 13.3 ms */
#define NX_GOODBYE_MS 1500u            /* power-off: goodbye screen before the PB2 signal */
#define NX_DONE_OFF_MS 15000u         /* "power off when all finished": the card counts down this long */
#define NX_AUTO_MS 10000u             /* auto-start at power-on: the card starts the programs after this */
#define NX_OFF_STOP_MS 300u            /* power-off: longest wait for both idle configurations */
#define NX_CAL_RELEASE_MS 10000u       /* calibration boot: longest wait for the keys' release */
#define NX_CAL_R80_WAIT_MS 110u        /* calibration boot: wait for G1's :w80 reply (app.c) */
#define NX_BOOTLOOP_N 3u               /* abnormal resets in a row before safe mode / power off */
#define NX_BOOTLOOP_WINDOW_MS 30000u   /* a reset after this much uptime ends the streak */
#define NX_UI_SLICE_US 2000u           /* UI render budget per main-loop pass */
#define NX_FW_ID 0x4E580401u           /* "NX", platform 4.1: written into the superblock */
#define NX_FW_VERSION "0.9.1"          /* the splash, Settings > About, Diagnostics; the release's name (make release) */
#define NX_PRODUCT "OpenGX Pro"         /* Settings > About: the project (the splash draws its own wordmark) */
#define NX_AUTHOR "Manuel Haller Polo"  /* Settings > About: the developer */

/* UI engine (docs/nx/design/ui.md §2): pixels per band buffer (two buffers, RGB565). 2560 =
 * 320 x 8; 1920 saves 2.5 KB of RAM (the design's cut line). */
#ifndef NX_BAND_PX
#define NX_BAND_PX 2560u
#endif

/* UI engine features the nx image never uses. They stay on here for the engine gallery
 * (APP=nxgallery, the host renderer) and the host tests; NX_ENG_OFF (src/nx/nx_config.mk) sets
 * them to 0 for the image, so they cost it no flash, and for the host renderer of its screens. */
#ifndef NX_ENG_VGRAD
#define NX_ENG_VGRAD 1       /* nx_vgrad and the renderer's gradient background (nx_render_bg) */
#endif
#ifndef NX_ENG_FADE
#define NX_ENG_FADE 1        /* the region fade (nx_trans_fade, nx_render_layer_fade) */
#endif
#ifndef NX_SCOPE_GATE
#define NX_SCOPE_GATE 1      /* scope: gating bands and their "NN %" label */
#endif
#ifndef NX_SCOPE_CUSTOM
#define NX_SCOPE_CUSTOM 1    /* scope: the custom 64-sample shape (NX_WAVE_CUSTOM) */
#endif
#ifndef NX_SCOPE_RATIO
#define NX_SCOPE_RATIO 1     /* scope: Out2 at a frequency ratio (NX_OUT2_INDEPENDENT) and its label */
#endif
#ifndef NX_SCOPE_LABELS
#define NX_SCOPE_LABELS 1    /* scope: amplitude and "wave #N ?" drawn inside the card */
#endif

/* Features beyond the original firmware (docs/nx/features.md). Each can be switched off (0) to
 * fit the image under a smaller proven flash limit; its strings and icons stay in the asset
 * tables. Off by default since 2026-10-05, to make room (docs/nx/drop1-size-inventory.md):
 * NX_FEAT_HIST and NX_FEAT_HERO48; make APP=nx NX_LCD_OPTS=-DNX_FEAT_HIST=1 builds one in again.
 * The host tests keep History on (tests/host/Makefile). */
#ifndef NX_FEAT_DIAG
#define NX_FEAT_DIAG 1       /* menu > Diagnostics and the key test, MCU temperature / supply */
#endif
#ifndef NX_FEAT_HIST
#define NX_FEAT_HIST 0       /* menu > History: every run in the SPI-flash ring, all-time totals */
#endif
#ifndef NX_FEAT_SLOTS
#define NX_FEAT_SLOTS 1      /* program detail > LOOP: play on both, copy / move / erase a slot */
#endif
#ifndef NX_FEAT_CHAINS
#define NX_FEAT_CHAINS 1     /* menu > Chains: programs one after the other on a generator */
#endif
#ifndef NX_FEAT_PLANS
#define NX_FEAT_PLANS NX_FEAT_CHAINS   /* menu > Chains: multi-day plans (a chain per day) */
#endif
#ifndef NX_FEAT_TERRAIN
#define NX_FEAT_TERRAIN NX_FEAT_CHAINS   /* menu > Chains: the built-in Terrain Protocol (NX_TERRAIN_DATA: the committed tables, src/nx/data/) */
#endif
#ifndef NX_FEAT_MANUAL
#define NX_FEAT_MANUAL 1     /* menu > Manual: a frequency or a sweep by hand */
#endif
#ifndef NX_FEAT_PRESET
#define NX_FEAT_PRESET NX_FEAT_CHAINS   /* menu > Chains: built-in presets with sweeps (NX_PRESET_DATA: the committed table, src/nx/data/) */
#endif
#ifndef NX_FEAT_TIMERS
#define NX_FEAT_TIMERS 1     /* menu > Timers: stop after N min per generator, power off when all finished */
#endif
#ifndef NX_FEAT_HERO48
#define NX_FEAT_HERO48 0     /* dashboard: the frequency in 48 px digits when it fits (else, and without: 28 px) */
#endif
#ifndef NX_FEAT_SUPPORT
#define NX_FEAT_SUPPORT 1    /* Settings > About > Support the project: the QR code of the donation page */
#endif

/* 1: the image that is published (`make release`): no artwork from the original image, and the
 * Terrain Protocol and the preset from the committed tables (src/nx/data/). */
#ifndef NX_RELEASE
#define NX_RELEASE 0
#endif
#if NX_RELEASE && (NX_VENDOR_SPLASH || !NX_TERRAIN_DATA || !NX_PRESET_DATA)
#error "release build: NX_VENDOR must be 0, NX_TERRAIN and NX_PRESETS 1"
#endif

/* ---- result codes of the nx modules (0 = success) ---- */
enum {
    NX_OK = 0,
    NX_REFUSED_PC = -1,   /* not allowed in PC mode (generator traffic, nx SPI-flash writes) */
    NX_BUSY = -2,         /* not now: before NX_GEN_READY_MS, calibration, queue full */
    NX_EINVAL = -3,       /* bad argument */
    NX_EGUARD = -4,       /* SPI-flash write outside the nx area */
    NX_EFLASH = -5,       /* SPI-flash busy timeout or read-back mismatch */
    NX_EREADONLY = -6,    /* nx storage is read-only (foreign data or a newer layout) */
    NX_ENOSPACE = -7,     /* the value does not fit */
    NX_ENOTFOUND = -8,    /* no such key */
    NX_EPENDING = -9,     /* another request still waits */
};
