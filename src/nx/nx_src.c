/* Program sources (nx_src.h). */
#include "nx_src.h"

#include <string.h>

#include "nx_bfb.h"
#include "nx_config.h"
#include "nx_manual.h"
#include "nx_preset.h"
#include "nx_terrain.h"
#include "nx_uicore.h"

int nx_src_fill(unsigned id, uint16_t ampl_cv, struct channel *c)
{
    int rc = NX_EINVAL;
#if NX_FEAT_TERRAIN
    if (nx_src_is_set(id))
        rc = nx_tset_fill(id - NX_SRC_SET, ampl_cv, &c->prog);
#else
    (void)ampl_cv;
#endif
#if NX_FEAT_MANUAL
    if (id == NX_SRC_MANUAL)
        rc = nx_manual_fill(&c->prog);
#endif
#if NX_FEAT_PRESET
    if (nx_src_is_preset(id))
        rc = nx_preset_fill(id - NX_SRC_PRESET, &c->prog);
#endif
    if (id == NX_SRC_BFB)
        rc = nx_bfb_fill(&c->prog);
    (void)id;
    if (rc == NX_OK)
        memset(c->params, 0, sizeof c->params);   /* no gating: what Spooky2 uploads */
    return rc;
}

uint32_t nx_src_dwell_s(unsigned id, unsigned i)
{
#if NX_FEAT_TERRAIN
    if (nx_src_is_set(id))
        return nx_tset_dwell_s(id - NX_SRC_SET, i);
#endif
#if NX_FEAT_PRESET
    if (nx_src_is_preset(id))
        return nx_preset_dwell_s(id - NX_SRC_PRESET, i);
#endif
    (void)id;
    (void)i;
    return 1;
}

unsigned nx_src_name(unsigned id, char *out)
{
    const char *s = id == NX_SRC_MANUAL ? "Manual" : "";
    if (id == NX_SRC_BFB)
        s = NX_BFB_NAME;
#if NX_FEAT_TERRAIN
    if (nx_src_is_set(id))
        s = nx_tset_name(id - NX_SRC_SET);
#endif
#if NX_FEAT_PRESET
    if (nx_src_is_preset(id))
        s = nx_preset_name(id - NX_SRC_PRESET);
#endif
    unsigned n = 0;
    for (; n < NX_UC_NAME_MAX && s[n]; n++)
        out[n] = s[n];
    out[n] = 0;
    return n;
}
