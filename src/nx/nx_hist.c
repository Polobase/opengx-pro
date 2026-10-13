/* History and usage totals (nx_hist.h). */
#include "nx_hist.h"

#include <string.h>

#include "nx_config.h"
#include "nx_core.h"
#include "nx_crc.h"
#include "nx_flash.h"
#include "nx_kv.h"
#include "nx_runner.h"
#include "nx_src.h"
#include "nx_time.h"
#include "nx_uicore.h"
#include "state.h"

#if NX_FEAT_HIST
_Static_assert(sizeof(struct nx_hist_rec) == NX_HIST_REC_BYTES, "history record layout");

#define TOTALS_VER 1u

static uint32_t s_seq;          /* the newest record's number (0: none) */
static uint32_t s_head;         /* ring position of the next record */
static uint8_t s_session_on;    /* this boot counted in the totals */
static struct nx_totals s_tot;
static struct nx_hist_rec s_pend[NX_HIST_PEND];
static uint8_t s_npend;
static uint8_t s_broken;        /* a flash error: no more writes this boot */

static struct {                 /* the run of each generator */
    uint8_t on, slot;
    uint32_t start_s, run_ms, last_ms;
} s_run[2];

static uint32_t rec_addr(uint32_t pos)
{
    return NX_HIST_START + (pos % NX_HIST_N) * NX_HIST_REC_BYTES;
}

static uint32_t seq_at(uint32_t pos)
{
    uint32_t v = 0xFFFFFFFFu;
    nx_flash_read(rec_addr(pos), &v, sizeof v);
    return v;
}

static int valid(const struct nx_hist_rec *r)
{
    return r->seq != 0xFFFFFFFFu && r->seq && r->crc == nx_crc32(0, r, NX_HIST_REC_BYTES - 4u);
}

void nx_hist_boot(void)
{
    s_seq = s_head = 0;
    s_npend = 0;
    s_session_on = s_broken = 0;
    memset(s_run, 0, sizeof s_run);
    memset(&s_tot, 0, sizeof s_tot);
    if (nx_kv_get(NX_KEY_TOTALS, &s_tot, sizeof s_tot) != (int)sizeof s_tot || s_tot.ver != TOTALS_VER) {
        memset(&s_tot, 0, sizeof s_tot);
        s_tot.ver = TOTALS_VER;
    }
    /* the sector whose first record is the newest valid one (a first record torn by a power
     * cut, or a sector half erased, does not count), then its last used record */
    if (nx_store_state() != NX_STORE_OK)
        return;   /* erased (nothing yet), foreign data or no store */
    uint32_t best = 0, sec = 0;
    for (uint32_t i = 0; i < NX_HIST_SECTORS; i++) {
        struct nx_hist_rec r;
        nx_flash_read(rec_addr(i * NX_HIST_PER_SECTOR), &r, sizeof r);
        if (valid(&r) && r.seq > best) {
            best = r.seq;
            sec = i;
        }
    }
    if (!best || nx_store_state() == NX_STORE_READONLY)
        return;   /* nothing yet, or not our data */
    uint32_t pos = sec * NX_HIST_PER_SECTOR;
    for (uint32_t i = 0; i < NX_HIST_PER_SECTOR; i++) {
        uint32_t q = seq_at(pos + i);
        if (q == 0xFFFFFFFFu)
            break;
        if (q > s_seq)
            s_seq = q;   /* a torn record still takes its place */
        s_head = pos + i + 1u;
    }
    s_head %= NX_HIST_N;
}

unsigned nx_hist_count(void)
{
    uint32_t cap = NX_HIST_N - NX_HIST_PER_SECTOR;   /* the sector ahead may be erased */
    return (unsigned)(s_seq < cap ? s_seq : cap);
}

int nx_hist_get(unsigned i, struct nx_hist_rec *r)
{
    if (i >= nx_hist_count())
        return 0;
    nx_flash_read(rec_addr(s_head + NX_HIST_N - 1u - i), r, sizeof *r);
    return valid(r);
}

const struct nx_totals *nx_hist_totals(void)
{
    return &s_tot;
}

int nx_hist_pending(void)
{
    return s_npend;
}

void nx_hist_add(unsigned ch, unsigned slot, uint32_t start_s, uint32_t run_s, uint8_t end)
{
    if (run_s < NX_HIST_MIN_S || !(nx_src_is_slot(slot) || nx_src_timed(slot) || slot == NX_SRC_MANUAL))
        return;
    if (s_npend == NX_HIST_PEND) {   /* full: the oldest waiting record goes */
        memmove(&s_pend[0], &s_pend[1], sizeof s_pend[0] * (NX_HIST_PEND - 1u));
        s_npend--;
    }
    struct nx_hist_rec *r = &s_pend[s_npend++];
    memset(r, 0, sizeof *r);
    r->ch = (uint8_t)ch;
    r->slot = (uint8_t)slot;
    r->start_s = start_s;
    r->run_s = run_s;
    r->end = end;
    if (slot < NAME_SLOTS) {
        strncpy(r->name, g_names[slot], NX_HIST_NAME < NAME_LEN ? NX_HIST_NAME : NAME_LEN);
    } else {   /* a built-in or manual source (nx_src.h) */
        char name[NX_UC_NAME_MAX + 1u];
        nx_src_name(slot, name);
        strncpy(r->name, name, NX_HIST_NAME);
    }
}

void nx_hist_end(unsigned ch, uint8_t end)
{
    ch &= 1u;
    if (!s_run[ch].on)
        return;
    if (s_run[ch].last_ms)
        s_run[ch].run_ms += nx_ms() - s_run[ch].last_ms;
    nx_hist_add(ch, s_run[ch].slot, s_run[ch].start_s, s_run[ch].run_ms / 1000u, end);
    s_run[ch].on = 0;
}

/* Starts, pauses and ends of the runs. */
static void watch(uint32_t now)
{
    for (unsigned ch = 0; ch < 2u; ch++) {
        const struct channel *c = &g_ch[ch];
        int run = (g_ui_flags & UIF_RUN(ch)) != 0;
        if (s_run[ch].on) {
            if (s_run[ch].last_ms)
                s_run[ch].run_ms += now - s_run[ch].last_ms;
            s_run[ch].last_ms = run ? now : 0u;
            int gone = g_ui_state == ST_PC || c->prog_slot != s_run[ch].slot || (!run && c->elapsed <= 1u);
            if (!gone)
                continue;
            nx_hist_add(ch, s_run[ch].slot, s_run[ch].start_s, s_run[ch].run_ms / 1000u,
                        g_ui_state == ST_PC ? NX_HEND_PC : NX_HEND_STOPPED);
            s_run[ch].on = 0;
        }
        if (run && g_ui_state != ST_PC && c->prog_slot) {
            s_run[ch].on = 1;
            s_run[ch].slot = c->prog_slot;
            s_run[ch].start_s = now / 1000u;
            s_run[ch].run_ms = 0;
            s_run[ch].last_ms = now;
        }
    }
}

static void write_one(void)
{
    struct nx_hist_rec *r = &s_pend[0];
    int rc = nx_store_prepare_write();   /* formats an empty region first */
    if (rc == NX_REFUSED_PC)
        return;
    if (rc != NX_OK) {
        s_broken = 1;
        return;
    }
    if (!s_session_on) {
        s_tot.sessions++;
        s_session_on = 1;
    }
    r->session = s_tot.sessions;
    r->seq = s_seq + 1u;
    r->crc = nx_crc32(0, r, NX_HIST_REC_BYTES - 4u);
    uint32_t a = rec_addr(s_head);
    if (s_head % NX_HIST_PER_SECTOR == 0u && !nx_flash_is_erased(a, 0x1000u) && nx_flash_erase(a) != NX_OK) {
        s_broken = 1;
        return;
    }
    rc = nx_flash_program(a, r, sizeof *r);
    if (rc == NX_REFUSED_PC)
        return;
    s_seq++;   /* also after a failed write: that place is used */
    s_head = (s_head + 1u) % NX_HIST_N;
    if (rc != NX_OK) {
        s_broken = 1;
        return;
    }
    s_tot.runs++;
    s_tot.run_s += r->run_s;
    if (r->slot >= 1u && r->slot <= 30u && s_tot.slot_runs[r->slot - 1u] < 0xFFFFu)
        s_tot.slot_runs[r->slot - 1u]++;
    nx_kv_put(NX_KEY_TOTALS, &s_tot, sizeof s_tot);
    s_npend--;
    memmove(&s_pend[0], &s_pend[1], sizeof s_pend[0] * s_npend);
}

void nx_hist_task(uint32_t now)
{
    if (g_ui_state == ST_CAL || !nx_core_ready())
        return;
    watch(now);
    if (!s_npend || s_broken || g_ui_state == ST_PC || nx_store_state() == NX_STORE_READONLY)
        return;
    int erase = s_head % NX_HIST_PER_SECTOR == 0u;
    if (nx_runner_quiet(erase || nx_kv_free() < 256u ? 1000u : 100u))
        write_one();
}

void nx_hist_power_off(void)
{
    if (g_ui_state == ST_CAL || !nx_core_ready())
        return;
    for (unsigned ch = 0; ch < 2u; ch++)
        nx_hist_end(ch, NX_HEND_STOPPED);
    if (g_ui_state == ST_PC || nx_store_state() == NX_STORE_READONLY)
        return;
    for (unsigned i = 0; i < NX_HIST_PEND && s_npend && !s_broken; i++)
        write_one();   /* nothing runs any more: no quiet moment to wait for */
}
#else
void nx_hist_boot(void)
{
}

void nx_hist_task(uint32_t now)
{
    (void)now;
}

void nx_hist_end(unsigned ch, uint8_t end)
{
    (void)ch;
    (void)end;
}

void nx_hist_power_off(void)
{
}

void nx_hist_add(unsigned ch, unsigned slot, uint32_t start_s, uint32_t run_s, uint8_t end)
{
    (void)ch;
    (void)slot;
    (void)start_s;
    (void)run_s;
    (void)end;
}
#endif
