/* The UI's view of the core (docs/nx/design/ui.md §6.2 "VM per generator", "System VM"): a
 * snapshot the UI builds once per main-loop pass through nx_uicore.h, compares with the one it
 * last invalidated (VM diff -> dirty rectangles) and copies at every frame start for drawing, so
 * one frame never mixes two core states. Plain data, no pointers into core memory. */
#pragma once

#include <stdint.h>

/* State of one generator as the dashboard shows it (ui.md §4.2). */
enum nx_gstate {
    NXG_NONE = 0,   /* no program loaded */
    NXG_READY,      /* loaded, at step 1, outputs off */
    NXG_RUNNING,
    NXG_PAUSED,     /* stopped in the middle (START resumes) */
    NXG_FINISHED,   /* ran to its end without loop (the UI's latch, cleared by the next action) */
};

struct nx_gen_vm {
    uint8_t state;        /* enum nx_gstate */
    uint8_t slot;         /* loaded program 1..30, 0 = none */
    uint8_t loop;         /* LOOP flag */
    uint8_t link_down;    /* the generator stopped answering (nx_txq_link_down) */
    uint8_t wave;         /* program header 0: waveform code (clamped to 255) */
    uint8_t stopped;      /* NXG_READY after STOP or the end (elapsed 1): the original's "Stopped" */
    uint16_t step;        /* current step, 1-based */
    uint16_t count;       /* steps of the program */
    uint16_t passes;      /* loop passes completed since the last play (UI counter) */
    uint16_t ampl_cv;     /* amplitude in 0.01 V peak to peak (program header 1, clamped) */
    uint16_t frac;        /* progress through the program, 0..65535 */
    uint16_t timer_min;   /* minutes left of its "stop after" time (rounded up), 0 = none */
    uint8_t chain;        /* the chain it plays, 1..4, 0 = none */
    uint8_t chain_item;   /* 1-based item of chain_n */
    uint8_t chain_n;
    uint8_t plan;         /* the multi-day plan the chain belongs to, 1..2, 0 = none */
    uint8_t plan_day;     /* 1-based day of plan_n */
    uint8_t plan_n;
    uint8_t terrain;      /* the Terrain protocol it plays, 1..6, 0 = none */
    uint8_t t_day;        /* its day, of t_days */
    uint8_t t_days;
    uint32_t dwell_s;     /* per step */
    uint32_t total_s;     /* the whole program */
    uint32_t left_s;      /* rest of the program (core-computed from its millisecond clock) */
    uint32_t step_left_s; /* rest of the current step */
    uint64_t freq;        /* current step's frequency (w24 encoding, state.h) */
    uint64_t next;        /* next step's frequency, 0 = none */
};

enum {   /* nx_sys_vm.pc_seen: what the PC set this session */
    NX_PCS_WAVE1 = 1, NX_PCS_AMPL1 = 2, NX_PCS_GATE = 4, NX_PCS_WAVE2 = 8, NX_PCS_AMPL2 = 16
};

struct nx_sys_vm {
    uint8_t ready;        /* the boot gate passed: generator actions are accepted */
    uint8_t pc;           /* PC mode (PB1): the PC owns the generators */
    uint8_t cal;          /* calibration menu (boot chord or factory ':a0') */
    uint8_t pc_out;       /* PC mode: outputs on, bit 0 G1 Out1, 1 G1 Out2, 2 G2 Out1, 3 G2 Out2 */
    uint8_t pc_sync;      /* PC mode: bit ch = Out2 follows Out1 (w14) */
    uint8_t cal_index;    /* selected calibration value 0..28 */
    uint8_t cal_fexp, cal_aexp;   /* calibration test frequency / level step exponents */
    uint16_t cal_sum;     /* checksum of the 29 calibration values: the grid repaints when it changes */
    uint16_t name_sum[4]; /* checksums of the names shown: G1's slot, G2's slot, PC slot 0, PC slot 31 */
    uint32_t uptime_s;    /* since power-on: the goodbye's session time, the night view's updates */
    uint64_t pc_freq[2][2];   /* PC mode: [ch][out] frequencies sniffed from w24/w25 */
    uint8_t pc_wave[2][2];    /* PC mode: [ch][out] waveform code (w20 / w21) */
    uint16_t pc_ampl[2][2];   /* amplitude, 0.01 V peak to peak, as the PC sent it (w28 / w29) */
    uint8_t pc_gate;          /* bit ch: per-step gating on (w12) */
    uint8_t pc_seen[2];       /* NX_PCS_* per generator */
    uint8_t pc_mult[2][2];    /* [ch][out] wave cycles in the selected table (nx_diag.h); 0 or 1: one */
};

struct nx_vm {
    struct nx_gen_vm g[2];
    struct nx_sys_vm s;
};

/* Core -> UI events (nx_ui_post): what a snapshot cannot show. */
enum nx_event_type {
    NXE_FINISHED = 1,   /* ch: a program ended (a = 1: it looped and goes on, 0: it stopped) */
    NXE_CAL_VALUE,      /* a = index: the factory port set one calibration value */
    NXE_CAL_ALL,        /* all calibration values changed (':a0', the :w80 reply at boot) */
    NXE_FLASH_ERROR,    /* a = nx result code of a failed settings write */
    NXE_TIMER_STOP,     /* ch: its "stop after" time was reached and it stopped (a = minutes) */
    NXE_ALL_DONE,       /* every program has finished: "power off when all finished" is due */
    NXE_SLOT_DONE,      /* a slot copy / move / erase ended: ch = 0 done, 1 failed, 2 no quiet moment; a = op << 8 | slot */
    NXE_ALERT,          /* ch: a Terrain Contact session has ended (its 0 Hz step: safe to pause); a = the next session */
    NXE_NO_TABLE,       /* ch: its generator did not answer a preset's wave table: the preset was stopped */
};

struct nx_event {
    uint8_t type;   /* enum nx_event_type */
    uint8_t ch;
    uint16_t a;
};

/* Header of a stored program slot, for the program detail. */
struct nx_slot_info {
    uint8_t used;         /* 1: the slot holds a valid program (count 1..200, dwell > 0) */
    uint8_t wave;
    uint16_t count;
    uint16_t ampl_cv;
    uint32_t dwell_s;
};


/* Diagnostics (menu > Diagnostics), read once a second while the screen is open. */
struct nx_diag_vm {
    uint32_t uptime_s;
    uint32_t boots;           /* boots since the crash record was created (power cuts clear it) */
    uint32_t crashes;         /* faults recorded since then */
    uint8_t reset_reason;     /* enum nx_reset (nx_fault.h) */
    uint8_t safe_mode;
    uint8_t store_state;      /* enum nx_store_state (nx_store.h) */
    char panel;               /* 'A' / 'B': LCD init variant */
    uint8_t jedec[3];         /* SPI-flash JEDEC id */
    uint8_t link_down[2];
    uint8_t sensors;          /* 1: t10 / vdd_mv are valid */
    int16_t t10;              /* MCU temperature, 0.1 degC */
    uint16_t vdd_mv;          /* supply */
    uint16_t lat_avg_us[2];   /* average :ok latency */
    uint32_t sent[2], timeouts[2];
    uint32_t uart_errors[2];  /* overrun + framing + noise per generator link */
    uint32_t pc_entries, pc_exits, w91_held;
    uint32_t flash_errors;    /* nx flash: busy timeouts + verify errors + guard refusals */
    uint32_t kv_free;         /* bytes left in the settings store page */
    uint32_t loop_gap_ms;     /* longest main-loop gap since boot */
};
