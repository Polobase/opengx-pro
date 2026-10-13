/* Internal interfaces shared by link.c, factory.c, calib.c and storage.c (not part of the
 * public link.h / storage.h contracts). */
#pragma once

#include <stdint.h>

/* SPI-flash layout (docs/spec/hardware.md §6.1) */
#define NAMES_ADDR          0x005000u
#define NAMES_BYTES         (32u * 60u)
#define PROG_SLOT_ADDR(n)   (0x010000u + (uint32_t)(n) * 0x1000u)
#define PROG_RECORD_BYTES   0x668u     /* u64 hdr[5] + u64 freq[200] */
#define PROG_PARAMS_OFFSET  0x668u
#define PROG_PARAMS_BYTES   800u       /* u16[400] */
#define CAL_ADDR            0x074000u
#define CAL_BACKUP_ADDR     0x075000u
#define CAL_BYTES           64u

/* storage.c: raw read of a stored program slot (for the :n* / :n# dumps). */
void storage_slot_read(unsigned slot, uint32_t offset, void *buf, uint32_t len);

/* factory.c: USART3 receive parser (interrupt context) and its reset. */
void factory_rx(uint8_t c);
void factory_reset(void);
/* factory.c: "<letter><number>" of the last received factory command (diagnostics). */
void factory_cmd_tag(char out[8]);

/* link.c: host access gate (protocols.md §2.4). Always open in the reimplementation because
 * UIF_PC_UNLOCK is always set; kept as the original expression. */
int link_gate_open(void);

/* link.c: "<letter><NN>" of the last frame header seen on ch (diagnostics, linktest app). */
void link_frame_tag(unsigned ch, char out[4]);

/* Compiler barrier: data written by the receive interrupt before it set a pending flag is
 * read only after the flag test. */
#define LINK_BARRIER() __asm volatile("" ::: "memory")
