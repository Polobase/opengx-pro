/* Made-up Terrain tables for the host tests (the real ones are the user's presets, generated into
 * build/nx/ by tools/gen_nx_terrain.py and never committed): the same shapes with invented names,
 * frequencies and short dwells. */
#include "nx_terrain_data.h"

#define F(c) (uint8_t)((c) & 0xFFu), (uint8_t)(((c) >> 8) & 0xFFu), (uint8_t)((c) >> 16)

static const uint8_t k_freq[] = {
    F(10000), F(25050), F(50),   /* list A: 100.00, 250.50, 0.50 Hz */
    F(123456), F(2182215),       /* list B: 1234.56, 21822.15 Hz */
};
static const uint8_t k_dwi[] = {
    0, 1, 0,   /* set 0: A with 3, 5, 3 s */
    1, 1,      /* set 1: B with 5, 5 s */
    2, 2, 2,   /* set 2: A x2 with 2 s each, then the end step */
    0, 2,      /* set 3: B x3 with 3, 2 s, then the end step */
};
static const uint16_t k_dwell[] = { 3, 5, 2, 4 };
static const struct nx_tset k_set[] = {
    { 0, 0, 0, 3, 1, 12, 50, NX_TSET_NO_END, 0 },
    { 13, 3, 3, 2, 1, 14, 50, NX_TSET_NO_END, 0 },
    { 0, 0, 5, 3, 2, 12, 200, 3, 0 },
    { 13, 3, 8, 2, 3, 14, 200, 3, 0 },
};
static const uint8_t k_item[] = {
    0, 1, 1 << 2 | 0,   /* Remote: day 1 */
    1, 1, 2 << 2 | 1,   /* day 2.1 */
    0, 1, 2 << 2 | 2,   /* day 2.2 */
    2, 2,               /* Contact: day 1 */
    2, 3,               /* day 2 */
    3, 3,               /* day 3 */
};
static const struct nx_tproto k_proto[] = {
    { 25, 0, NX_TP_REMOTE, 3, 2, 0 },
    { 25, 9, NX_TP_CONTACT, 3, 3, 0 },
};

const struct nx_tdata k_nx_terrain = {
    k_freq, k_dwi, k_dwell, k_set, k_proto, k_item, "Demo cleanse\0Demo kidney\0Demo", 4, 2,
};
