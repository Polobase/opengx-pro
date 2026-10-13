/* Built-in Terrain Protocol tables (docs/nx/features.md, "Terrain Protocol"): generated from
 * Spooky2's Terrain presets by tools/gen_nx_terrain.py into src/nx/data/nx_terrain_data.c
 * (committed; `make nx-data` writes it again, `make APP=nx NX_TERRAIN=0` builds without), and made
 * up for the host tests (tests/host/nx/terrain_fixture.c).
 *
 * A set is a frequency list played k times, each entry with its own dwell, then optionally a 0 Hz
 * end step (the Contact sets' "safe to pause" notification). A protocol is either a Remote chain
 * (sets that each loop for their hours, then the next, forever) or a Contact schedule (days of
 * two sessions, one set each). */
#pragma once

#include <stdint.h>

#define NX_TSET_NO_END 0xFFu

struct nx_tset {
    uint16_t name;   /* offset of its name in str */
    uint16_t list;   /* first entry of its frequency list in freq */
    uint16_t dw;     /* first of its n dwell indices in dwi (entry i of every pass: dwi[dw + i]) */
    uint8_t n;       /* entries in the list */
    uint8_t k;       /* the list plays k times */
    uint8_t wave;    /* generator waveform table (w20/w21): Spooky2 wave w is table 10 + w */
    uint8_t dv;      /* amplitude, 0.1 V */
    uint8_t end;     /* dwell index of the 0 Hz end step, NX_TSET_NO_END: none */
    uint8_t rsv;
};

enum { NX_TP_REMOTE = 0, NX_TP_CONTACT = 1 };

/* NX_TP_REMOTE item (3 bytes): set, hours, label = day << 2 | part (0: the day "N.", 1/2: "N.1"/"N.2").
 * NX_TP_CONTACT day (2 bytes): the sets of session 1 and 2. */
struct nx_tproto {
    uint16_t name;   /* offset of its name in str ("Terrain", "Terrain-Mercury", ...) */
    uint16_t item;   /* first byte of its items or days in item */
    uint8_t kind;    /* NX_TP_REMOTE, NX_TP_CONTACT */
    uint8_t n;       /* items (Remote) or days (Contact) */
    uint8_t days;    /* days of one round (11, or 10 for Terrain-Nonviolent) */
    uint8_t rsv;
};

struct nx_tdata {
    const uint8_t *freq;       /* 3 bytes per list entry: centi-Hz, little-endian */
    const uint8_t *dwi;        /* dwell indices */
    const uint16_t *dwell;     /* seconds */
    const struct nx_tset *set;
    const struct nx_tproto *proto;
    const uint8_t *item;
    const char *str;           /* NUL-terminated names */
    uint8_t nset, nproto;
};

extern const struct nx_tdata k_nx_terrain;
