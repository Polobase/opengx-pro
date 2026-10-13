<!-- design note of 2026-09-23: Storage v2, runner v2, # protocol, PC monitor, feature feasibility.
Superseded where docs/nx/fw2-design.md decides differently. -->

# nx: Storage v2 and Runner v2

> **Scope (2026-09-24):** Drops 3 and 4 (presets, Terrain, chains, plans, archive, manual mode, sweeps and the rest) are cancelled, and of Drop 2 only resume after a power cut remains (on the KV store, no journal ring). Sections that described only cancelled work were removed; what is left may still mention it in passing. Current scope: `docs/nx/fw2-design.md`.

## 0. Findings that shape the design

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| F1 | A `:` anywhere starts a frame in the display parser. In `RX_IDLE`, a `:` starts a frame and `:`+`o`+`k` sets `gen_ok`, regardless of position in the line. The generator parser probably resyncs the same way. | `src/link.c` lines 275–290 | **No `#` line (request or reply) may ever contain byte 0x3A.** Names and binary data are sent as `%XX` escapes or base64 (the base64 alphabet has no `:`). |
| F2 | The generators answer at least one read that the display sends in standalone mode. At calibration boot, `:r80=0,` goes out on U1 with PB2 still low, and the `:w80=…` reply is parsed. The BACKLOG statement "no answer outside a Spooky2 session" was measured from the PC side. | `app.c` lines 322–337, `BACKLOG.md` | Spike S1 is promising, but it must be measured for each register and for each PB2 level. |
| F3 | The STM32G070 has only POR/PDR: no PVD and no programmable BOR. The backup registers (5×32 bit) survive a system or IWDG reset but are lost at power-off unless VBAT is powered. | Datasheet §3.7, §3.16 | Power cannot be predicted. Every flash structure must survive a cut at any instant: a torn page program or a half-erased sector. |
| F4 | `Terrain (R) - BY.txt` is a preset chain of 15 `[Preset]` blocks. Each block has `Base_Preset=\Detox\Shells\Healing (R)` (or Killing), one `Loaded_Frequencies` list (18–37 frequencies, 78–1800 s each) and `Schedule_Run_For=24` or `12` (hours). The day number is encoded in `PresetName` ("6.1", "6.2"), not in a key. | a Spooky2 installation | A multi-day PLAN is an ordered list of (preset, run-for) items grouped into days. |
| F5 | The Healing (R) shell uses `Out1_User_Defined_1=True` with `UDB_Square`: a 1024-sample CSV, +0.95/−0.89 asymmetric square. Killing (R) uses `Out1_Inverted_Sawtooth`. Both set `InverseWave=True`. The `W2`/`W4` suffix on the first frequency matches Spooky2's waveform numbering (2 = square, 4 = inverted sawtooth). | shells, `Waveforms/UDB Square.csv` | The per-item waveform override is in scope. The upload protocol for arbitrary waveforms is unknown. The stored hdr0 values 41…48 may be generator waveform banks. Both need spike S6. |
| F6 | Frequency syntax seen: `f=dwell`, `a-b=duration` (sweep), `f=dwell Wn`, `~6715BC059566=180` (DNA database references). The install holds 26,603 presets (181 MB). | install | The device stores a subset the user picks. DNA references need the user's `.s2d` database, so they come later. |
| F7 | Flash writes block the main loop: page program ≤3 ms, sector erase ≤400 ms, 64 KB block ≤2 s. | W25Q16 data | **nx does no SPI writes of its own while `ui_state == ST_PC`.** The only exceptions are explicit `#` requests. |
| F8 | The Makefile wildcards `link*`, `storage*`, `runner*`, `ui*`, `gfx*` would pull new files into the `fw` build. | `Makefile` | All new modules use the prefix `nx_`. Hooks inside shared files compile only under `-DAPP_NX`, so `APP=fw` stays unchanged. |
| F9 | `fw` uses 11.2 KB of 36 KB RAM and 31.4 KB of flash. The bootloader is proven only up to 74.7 KB images (112,640 bytes since the size test of 2026-09-25). | `size`, `docs/nx/fw2-design.md` | Preset compiling stays on the Mac. Programs are streamed from SPI flash (small cache) instead of copied into 2×2.4 KB of RAM. |
| F10 | PB8/PB9 on the factory JST are FT_f pins. AF2 = TIM16_CH1 / TIM17_CH1, AF6 = I2C1_SCL/SDA, AF0 on PB9 = IR_OUT. `TS_CAL1` is at 0x1FFF75A8 (30 °C), `VREFINT_CAL` at 0x1FFF75AA, average slope 2.5 mV/°C. | Datasheet tables | This confirms the hardware add-on and diagnostics options. |

---

## 1. Storage v2 (0x080000–0x1FFFFF)

### 1.1 Rules
1. All nx data lives in 0x080000–0x1FFFFF. The layer `nx_flash_*` enforces this with a range guard: a write outside the range is refused and counted, never executed.
2. Legacy data (names 0x5000, slots 0x11000–0x2E000, calibration 0x74000/0x75000) is written only through `storage.c`, i.e. the existing code paths. Two small additions are needed for on-device slot operations (§1.9), and they use the identical layout.
3. No nx-initiated flash I/O in PC mode (F7). Legacy hooks only mark RAM state as dirty.
4. Power-fail safety comes from five patterns:
   - append-only records with a sequence number and a CRC;
   - commit words programmed last (NOR only clears bits);
   - A/B copies for the superblock;
   - a sector header written only after its erase completes;
   - write-new, commit, then tombstone the old copy, with repair at mount.
5. CRC-32 uses the hardware CRC unit: POL 0x04C11DB7, INIT 0xFFFFFFFF, REV_IN=byte, REV_OUT=1, final XOR in software. This equals zlib/`binascii.crc32`, so the Python tools compute the same value. On the host a bitwise software version is used, and the vector "123456789" = 0xCBF43926 is checked on the device.

### 1.2 Region map (64 KB aligned where it helps bulk erase)

| Range | Sectors | Name | Content |
|---|---|---|---|
| 0x080000–0x080FFF | 1 | SB-A | superblock A |
| 0x081000–0x081FFF | 1 | SB-B | superblock B |
| 0x082000–0x089FFF | 8 | KV | key-value log: 2 pages of 16 KB (4 sectors each). Holds settings, favourites, recent, chains, plan cursors, totals, slot catalogue, mapping tables |
| 0x08A000–0x08BFFF | 2 | DIAG | results of the generator spike, last fault record, raw BFB scan buffer |
| 0x08C000–0x08DFFF | 2 | XFER | intent/scratch record for power-safe slot copy, move and restore |
| 0x08E000–0x08FFFF | 2 | RSV | reserved |
| 0x090000–0x09FFFF | 16 | JRNL | run journal ring, 64-byte records |
| 0x0A0000–0x0BFFFF | 32 | HIST | history ring, 32-byte records (~4,000 events) |
| 0x0C0000–0x1FFFFF | 320 | HEAP | object heap (1.25 MB): archive entries, preset packs, waveform objects, BFB results, slot shadows, optional UI asset packs |

UI assets are not a fixed region: they are the heap object type `ASSET`, so the UI allocates space dynamically.

### 1.3 Shared low-level modules
- `src/nx_flash.c/.h`:
  - `nx_flash_read`, `nx_flash_program` (range guard plus read-back verify), `nx_flash_erase_sector`, `nx_flash_is_erased(addr,len)`;
  - an IWDG refresh inside the busy-poll loops, through a weak `spiflash_idle_hook()` added to `spiflash.c` wait loops (empty for `fw`);
  - a bad-sector bitmap (KV key `BADSECT`).
- `src/nx_crc.c/.h`: `nx_crc32(init, buf, len)` (hardware or software) and `nx_crc16_ccitt()` for `#` lines.
- `src/nx_ring.c/.h`: a generic append-only ring with fixed record size. It is used by JRNL (64 B) and HIST (32 B).

### 1.4 Superblock (A/B, rewritten only at format or migration)
```c
struct nx_super {                 /* at 0x080000 and 0x081000 */
    uint32_t magic;               /* 'NXSB' */
    uint16_t layout_ver;          /* 1 */
    uint16_t min_reader_ver;      /* oldest nx allowed to WRITE this layout */
    uint32_t seq;                 /* the higher valid copy wins */
    uint32_t kv_start, kv_sectors, jrnl_start, jrnl_sectors, hist_start, hist_sectors, heap_start, heap_sectors;
    uint32_t created_by_fw;       /* nx build id */
    uint32_t format_count;
    uint8_t  mcu_uid[12];         /* informational: flash chip moved to another board */
    uint32_t crc32;               /* over all preceding bytes */
};
```
Mount rules:
- No valid copy and the region is all 0xFF: format by writing B, then A. Sectors are erased lazily later.
- No valid copy but non-0xFF data is present: go **read-only** and show a warning. This guards against a future vendor firmware using the region. The only way out is an explicit "Erase extended storage" action.
- `min_reader_ver` greater than our version: read-only.

### 1.5 KV store (`src/nx_kv.c`)
- Two pages of 16 KB. The page header (32 B) is `{'NXKV', gen, ACTIVE word, crc}`. ACTIVE is programmed last, after a compaction completes.
- Entries are 16-byte aligned:
  ```c
  struct nx_kv_ent { uint16_t magic /*0x4B56*/, key, len /*0 = delete*/, hcrc16; uint32_t seq, vcrc32; }; /* + value */
  ```
- Mount picks the page with the highest valid `gen` that has ACTIVE set. It then scans the entries into a RAM index (≤64 keys × 8 B = 512 B):
  - `hcrc` bad: the rest of the page is untrusted, so it counts as full.
  - `vcrc` bad: skip that entry and continue.
- Compaction:
  1. Erase the other page.
  2. Write the header without ACTIVE.
  3. Copy the latest value of every key, plus the new value.
  4. Program ACTIVE.

  A cut at any step leaves the old page valid.
- The live set is capped at ~12 KB.

| Key | Value |
|---|---|
| 0x0001 `SETTINGS` | `struct nx_settings` (versioned, below) |
| 0x0002 `BOOT` | `u32 boot_no`, `u32 abnormal_reset_streak` |
| 0x0003 `SLOTCAT` | 30 × CRC-32 of each slot's 0x988 bytes (0 = empty) + CRC of the names table |
| 0x0004 `FAVS` / 0x0005 `RECENT` | 16 / 8 × `struct nx_ref` |
| 0x0006 `TOTALS` | run seconds per channel, sessions, PC sessions, PC seconds |
| 0x0007 `AUTOSTART` | `nx_ref`, channel, delay |
| 0x0010–0x001F `CHAIN[16]` | `nx_chain_hdr` + ≤32 × `nx_chain_item` (≤544 B) |
| 0x0030–0x003F `PLANCUR[16]` | `{plan_uid, pack_crc, day, item, ch, policy, done_cs, started_boot}` |
| 0x0050 `GENINFO` | generator firmware versions, spike summary |
| 0x0060 `WAVEMAP`, 0x0061 `OFFSMAP`, 0x0062 `ENCODING` | **data-driven mapping tables** (waveform code → shape, w32 code → offset %). Spike results can be installed with `nx_ctl.py` without reflashing. |
| 0x0070 `BADSECT` | bitmap |

```c
struct nx_ref { uint8_t kind, flags; uint16_t aux; uint32_t id; };  /* SLOT(1..30) ARCH(uid) LIB(entry uid, aux=pack uid) CHAIN(n) PLAN(uid) MANUAL(n) */
struct nx_chain_hdr  { uint8_t ver, n_items, flags /*LOOP,MIRROR,SKIP_MISSING*/, ch_pref; uint16_t loops /*0=inf*/, gap_s, session_min, rsv; char name[20]; };
struct nx_chain_item { struct nx_ref ref; uint8_t repeat, gap_s; uint16_t rsv; uint32_t ref_crc /* slot content at add time */; };
struct nx_settings_v1 { uint8_t ver, backlight /*1..16, 16=100%*/, dim_level, night_mode;
    uint16_t dim_after_s, off_after_s; uint8_t resume_mode /*OFF,ASK,AUTO_COUNTDOWN,AUTO*/, resume_after_clean_off,
    resume_countdown_s, keylock_auto_min; uint16_t session_limit_min[2]; uint16_t sweep_tick_ms;
    uint8_t live_apply /*0 = safe pause/apply/resume*/, ampl_cap_V /*0 = none*/, lang, rsv; };
```

### 1.9 Legacy events

| Event | Code path | nx hook (RAM only, `#ifdef APP_NX`) | Deferred action (standalone, idle) |
|---|---|---|---|
| `:pNN` upload | `storage.c` `program_save` | `nx_evt_slot_changed(NN)` | recompute `SLOTCAT[NN]`. Chain items whose `ref_crc` differs get a "changed" badge and are played by reference, the way Spooky2's GX-offline slots work. Resume records with a different `prog_crc` are refused. The SLOT_SHADOW (N6) keeps the previous version. |
| `:nNN=,` | `program_erase` | same | `SLOTCAT[NN]=0`. Chain items become "missing": skipped if `SKIP_MISSING`, otherwise the chain stops with a message. |
| `:nNN=text` | `names_save` | `nx_evt_names_changed()` | refresh the library view |
| `w96` | `storage_erase_all` | `nx_evt_erased_all()` | all SLOTCAT entries become 0, HIST LEGACY_EVT, banner "All slots erased by PC, restore from archive?". nx never adds work before `w96`, because that would lengthen the byte-drop window. |
| Slots changed while nx was not running (vendor fw or `fw` build) | – | – | at boot, fresh CRCs vs SLOTCAT (30 × 2.4 KB read ≈ 50 ms during the splash), handled the same way |
| Calibration | `cal_*` | none | none (nx never writes calibration) |

On-device slot delete, copy, move and restore-from-archive go through `storage.c`:
- `program_save` is refactored into `program_save_from(slot, const struct program*, const uint16_t*)`. `program_save(slot, ch)` becomes a one-line wrapper around it, and the existing byte-for-byte test against the real backup proves `fw` is unchanged.
- `name_set` plus `names_save` handle the names.
- Atomicity: the source image is first written to XFER with header `{op, dst, crc, commit}`. Then the slot is erased and written, and the XFER entry is tombstoned. At boot, a committed XFER entry that was never tombstoned means the operation is redone, which is idempotent.

### 1.10 Versioning and migration
- `layout_ver` and `min_reader_ver` in the superblock. Migrations are functions `nx_migrate_vN_to_vN+1()`, run on mount under a KV write lock.
- Each record type carries its own `ver`/`fmt`. `nx_settings` is migrated field by field with defaults.
- Pack `min_reader`: a pack from a newer tool shows "needs newer firmware".
- Downgrade to an older nx means the region is read-only. Flashing back the original does not touch the region, and nx later remounts it unchanged.

---

## 2. Runner v2

### 2.1 Layers
```
sources (nx_src_*.c)  ->  play engine (nx_runner.c, per channel)  ->  sequences (nx_seq.c)  ->  genq (nx_genq.c, per link)  ->  link_send_* (link.c)
       ^ nx_pack / nx_obj / storage.c          ^ TIM3 exact 10 ms             ^ journal/hist writes (nx_journal.c)
```
- **Exact timer.** In nx's own tick hook (`nx_app.c`), `elapsed_cs` of a running channel increases every 100 TIM3 ticks instead of 101. This fixes the vendor's 1 % long timer.
- **`nx_genq`, one queue per link.** Up to 16 compact commands (`{kind LITERAL|W_U64|W_PAIR5, id, a, b, u64 v, flags COALESCE}`, 12 B). The main loop sends one command at a time and does not block. A command completes on the **per-link** `:ok` counter (the new hook in the `link.c` ok matcher increments `g_link_ok_cnt[ch]`), or on a 40 ms timeout counted with `g_ticks`. After completion comes the original 133 µs tail.
  - Counters per link: sent, ok, timeout, `:err` (a new matcher), and latency min/avg/max plus a 16-bucket histogram.
  - Frequency-only commands are "latest wins": a still-pending `w24`/`w25` is replaced.
  - G1 and G2 run in parallel. The per-link order is the original's; only the interleaving between links differs, which is a documented nx deviation.

### 2.2 Sequences (`nx_seq.c`)

| Sequence | Commands (per link) | Status |
|---|---|---|
| IDLE_CFG(f, w1,w2, a1,a2, o1,o2, sync) | `:w24=f,` `:w11=0,0,` `:w14=1,` `:w17=0,1,` `:w20=w1,` `:w21=w2,` `:w28=a1,` `:w29=a2,` `:w32=o1,` `:w33=o2,`. When Out2 is independent: `:w14=0,` then `:w25=f2,` in place of `:w14=1,` | legacy when w1=w2, a1=a2, o1=o2 and sync; independent Out2 needs S1-T6 |
| PLAY_STEP(f, a, b) | `:w24=f,` [`:w25=f2,`] `:w11=1,1,` then `:w12=0,0,` or `:w50=aaaaa,bbbbb,` `:w51=…` `:w12=1,1,` | legacy |
| END | `:w11=0,0,` | legacy |
| SWEEP_TICK | `:w24=f,` [`:w25=f2,`] | Spooky2 BFB does this at ~13.5 steps/s inside a session |
| ITEM_CHANGE (chain/preset program/waveform switch) | IDLE_CFG(new step 1), [gap], PLAY_STEP | same bytes as a legacy pause followed by start; avoids the untested live `w28`/`w20` change |
| LIVE_AMPL / LIVE_WAVE | `:w28=` / `:w20=` while outputs are on | experimental, only if `live_apply` is set and S1-T5 passed |

**Guarantee for plain slot playback.** For every key sequence, nx sends per link the same lines in the same order as `runner.c`. The documented differences:
- exact 10.00 ms units;
- restart at 0, not 1;
- parallel links with per-link acks;
- after PC-mode exit, nx sends the channel's idle config instead of the one from the last upload.

This is proven by a host differential test (§7) and a serial-only emulator comparison.

### 2.6 Session timer, resume, auto-start, watchdog
- **Session timer:** counts running time only. When it reaches the limit, END plus IDLE_CFG and HIST(TIMER). The remaining time is stored in the journal.
- **Boot flow:**
  1. Mount storage (≈30–60 ms, during the splash).
  2. Read RCC_CSR, clear it with RMVF, and check for safe mode.
  3. Splash. Keep ≥4 s before the first generator TX until S1-T0 has measured when the generators become ready.
  4. PB1 high: PC mode. No resume or auto-start; a pending resume is offered after PC exit.
  5. Legacy boot IDLE_CFG to both links.
  6. Resume check.
  7. Otherwise auto-start.
  8. Otherwise HOME.
- **Resume:** the last valid record is SNAPSHOT or PC_INTERRUPTED, or CLEAN_OFF when `resume_after_clean_off` is set. For each channel:
  - the source must exist and `prog_crc` must match, otherwise the channel is skipped with the reason;
  - the channel is restored at the snapshot position (at most 60 s lost), paused if it was paused, and the order is ITEM_CHANGE then running;
  - modes: OFF, ASK (wait for a key), AUTO_COUNTDOWN (default 15 s; START resumes now, STOP discards and writes IDLE, G1/G2 toggle a channel), AUTO;
  - HIST records POWER_CUT_DETECTED and RESUME.
- **Auto-start** (slot, chain, preset or plan per channel) runs only when:
  - PB1 is low;
  - the reset was power-on or pin, not safe mode;
  - there is nothing to resume, or resume was declined;
  - no key is held at boot (G1+G2 means calibration, POWER+STOP means bootloader).

  A 10 s countdown lets any key cancel. If S1 shows acks exist and a link had 100 % timeouts at boot, a warning icon is shown.
- **Watchdog:** the IWDG starts after init (LSI, prescaler 64, reload 2000 ≈ 4 s) and is refreshed every main-loop pass, in the flash busy hook, and inside nx's power-off toggle loop so an "off but powered" unit never reboots.
  - After an IWDG reset: a fast path skips the splash, restores the exact state from a `.noinit` mirror (magic + CRC; survival through the bootloader is checked in S4), or else from the journal, and resumes within about 0.5 s with HIST WDT_RECOVERY.
  - Fault handler: registers and a stack snippet go to `.noinit`; HIST FAULT and the DIAG sector are written after reboot.
  - Boot-loop guard: 3 abnormal resets each within 120 s of uptime → safe mode (no resume or auto-start; diagnostics screen).

## 7. Tests

**Host** (`tests/host/`, a second binary `test_nx` built with `-DAPP_NX`; the existing `test_link` stays unchanged):

`w25q16.c` extensions:
- `flash_cut_at(op_index, TEAR_PAGE|TEAR_ERASE|CLEAN)`: a torn page program writes a random subset of bits or bytes; a torn erase sets random bits back to 1 over part of the sector.
- `flash_erase_count[512]`.
- `flash_guard(lo, hi)` counting violations; it must stay 0 for every nx-only test.

Mock extensions:
- `sim_advance_ms()`, which runs the tick hook.
- Generator modes per link: ACK(delay), NOACK, ERR, REPLY_TABLE, ACK_OTHER_LINK (checks the shared-flag quirk is gone in nx).

Test sets:
1. `test_nx_store.c`
   - format and mount; A/B superblock;
   - KV put/get/delete/compaction; heap alloc/commit/supersede/tombstone; ring rotation;
   - **crash consistency: for every scenario and every flash operation index i, cut at i, remount, check invariants** (no committed data lost, no torn record accepted, repairs complete);
   - migration from a stored v1 image;
   - read-only mode on foreign data;
   - legacy hooks (`w96`, `:n,`, `:p` → SLOTCAT, chain badges, resume refusal);
   - a 10-year compressed wear simulation (asserts the max erase count against §1.6);
   - C/Python cross-check: C writes an image, `nxfmt` parses it, and the reverse.
2. `test_nx_runner.c`
   - **Legacy equivalence:** fuzz 10k random programs and key sequences, run the `fw` `runner.c` and nx's SLOT source, and compare the per-link TX logs byte for byte (with timing normalised);
   - chain golden sequences (repeat, gap, loop, missing items);
   - plan day advance (RUNTIME, SESSION);
   - session timer;
   - resume from a journal (including a torn last record);
   - auto-start decision table (PB1, reset reason, safe mode, key held);
   - sweep frequency progression and coalescing under NOACK;
   - manual safe apply;
   - genq timeout and latency statistics.
3. `test_nx_ext.c`
   - grammar, CRC, escaping and base64;
   - `obj` transactions (abort, bad CRC, power cut, retry idempotency);
   - **differential link test:** the same byte streams (existing vectors, serial captures of Spooky2, and random `: # \n , . digit` fuzz) fed into the plain `link.c` and the `APP_NX` build must produce identical reply bytes, identical `UIF_LINE` set/clear byte indices and identical `g_ch` contents.
4. `test_nx_mon.c`: sniffed values and statistics from replayed captures; r-matcher whitelist (r80 and r90 never stored).

**Emulator** (`tools/sim`):
- New peripheral models: CRC, IWDG (key/prescaler/reload, reset event), ADC (fixed VREFINT/TS), RCC_CSR flags, `.noinit` preserved across emulated resets.
- New timeline events: `{"power_cut": true, "tear": "next_flash_op"}`, `{"reboot": true}` (same SPI image), `{"expect_tx": {"port":"U1","regex":…}}`.
- Generator config: `{"U1":{"ack_ms":2,"replies":{":r02=0,":":r02=200.\r\n"}},"U2":{"ack":false}}`.
- `spi_image` fixtures built with `nxfmt` (`tools/sim/nxfixture.py`).
- `diff.py --serial-only`.

Scenarios (`tools/scenarios/nx_*.json`):

| Scenario | Checks |
|---|---|
| `nx_serial_compat_*` | re-runs queries, serial_storage, serial_maint, serial_adversarial, pc_mode and upload_run against the reference, serial-only |
| `nx_boot_idle` | boot idle config vs fw |
| `nx_chain_run` | a chain plays with the expected sequences |
| `nx_resume_powercut` | cut at 95 s, then reboot |
| `nx_resume_tear` | power cut during a journal write |
| `nx_autostart`, `nx_autostart_pc`, `nx_autostart_cancel` | auto-start and its safety checks |
| `nx_session_timer` | auto-stop |
| `nx_manual`, `nx_sweep` | manual mode and sweep command sequences |
| `nx_watchdog` | a test-build-only `#dbg.hang` triggers IWDG recovery |
| `nx_pc_monitor` | monitor screen and statistics |
| `nx_ext_upload` | upload a pack, browse, play; flash operations only in the region |
| `nx_w96_after_chain` | chain items marked missing after `w96` |
| `nx_terrain_day_advance` | shortened run-for, power cut in the middle of a day |

---

## 9. Main risks

| Risk | Mitigation |
|---|---|
| Generators do not ack in standalone: 40 ms per command, idle config ≈ 400 ms | non-blocking genq keeps the UI responsive; rate limits adapt to the measured latency |
| Waveform, offset and gate mapping unknown | data-driven KV maps that can be installed without reflashing; APPROX flags; UDB waveforms approximated |
| Stall from a flash erase | never in PC mode; pre-erase when idle |
| Torn or weak erase | ring headers written only after the erase; read-back verify; bad-sector map |
| Image size limit | compiler on the Mac, streaming instead of RAM copies |
| `#` collisions | no `:` ever; differential fuzz tests |
| Vendor data | only read from `original/` and from `TERRAIN_DIR` / `PRESET_FILE` (an untracked `local.mk`); embeds only in `build/`. Since 2026-10-13 one stated exception: the two tables `make nx-data` makes from the preset files are committed in `src/nx/data/` (`docs/nx/THIRD_PARTY.md`) |

### Critical Files for Implementation
- src/link.c
- src/storage.c
- src/classic/runner.c
- src/spiflash.c
- tests/host/w25q16.c
