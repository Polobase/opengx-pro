<!-- design note of 2026-09-23: Firmware architecture, UI inventory and protocol boundary.
Superseded where docs/nx/fw2-design.md decides differently. -->

# OpenGX Pro: architecture, UI inventory and compatibility boundary for a new UI

Sources: src/*.c/h (including board, uart, spiflash, fmt, startup), DEVIATIONS.md, BACKLOG.md, docs/serial-commands.md and docs/spec/{protocols,application,display}.md; the relevant parts of docs/spec/hardware.md, the Makefile, ld/app.ld, the build map/ELF, the conformance and latency result files, and the emulator's peripheral list.

**In short.** The PC protocol never looks at screen state. link.c never reads `g_ui_state`. Almost everything in ui.c, gfx.c, lcd.c and the key mapping can be redesigned freely. The parts that must stay exactly as they are:
- the receive parser (in the UART interrupt) and the reply builders in link.c;
- which `w` commands are handled at once and which wait for the main loop;
- the "active program buffer = upload buffer" rule;
- the PC-mode entry/exit and `w92`/PB2 sequences, including their timing;
- the SPI-flash sectors the original uses.

---

## A. Architecture

### A.1 Boot (`main_fw.c:11-24`, `app.c:301-347`)
1. `clock_init` (64 MHz from the 8 MHz crystal, or HSI fallback; `board.c:28-70`), then `board_init`. That starts TIM3 at 10 kHz, priority 0 (`board.c:123-135`). Then `uart_init`, `link_init`, `spiflash_init`, and interrupts are enabled.
2. `delay_loop(0x82355)` (≈33 ms), then `lcd_init` (reset 2×27 ms, panel variant A/B read from 0x08003C00, 160 ms after SLPOUT; `lcd.c:123-147`).
3. `app_run`:
   - `names_load` and `cal_load`;
   - both channel buffers set to the default program {0,0,120,1,1};
   - screen cleared, backlight duty 8, `ui_draw_boot_splash`.
4. The first key scan (13.3 ms) checks for G1+G2:
   - **Held:** calibration mode. The firmware sends `:r80=0,` on U1, waits up to 110 ms for `:w80=` (`app.c:322-337`), then sets PB2 high and sends `cal_apply_test()` (`app.c:342-345`).
   - **Not held:** the splash stays for 100 × `delay_loop(640000)` ≈ 4 s, and ends early when a USART3 frame arrives (`app.c:338-341`).
5. The main loop starts (`app.c:349-364`).

### A.2 Main-loop pass (`app.c:349-364`)

| # | Step | Cost / notes |
|---|---|---|
| 1 | `key_event_update` → `key_scan` = `delay_loop(0x34155)` + `keys_read` (`app.c:48-69`) | **Always 13.3 ms**. A new press does a second scan, so **26.7 ms**. The press event needs two consecutive scans. |
| 2 | Power-off check: `key_event == POWER\|HELD && hold > 30` (`app.c:351-352`) | about 0.8 s of holding |
| 3 | `pc_mode_update` (`app.c:104-135`) | Checks the PB1 edge. Entry blocks 2×13.3 ms plus 2 ack waits (up to 40 ms each). |
| 4 | `dispatch_keys` by state (`app.c:257-285`) | |
| 5 | `runner_tick` unless in PC or CAL state (`app.c:355-356`) | **Blocking.** Up to 10 commands per channel, each followed by `link_wait_ok` (up to 40 ms). |
| 6 | `link_process(0)`, then `link_process(1)` if a line is pending (`app.c:357-360`) | U1 always before U2. Flash writes happen here before `:ok`. |
| 7 | `ui_render` (`ui.c:411-470`) | full redraw if `UIF_REDRAW`, plus marquee and PC incremental updates |
| 8 | `link_factory_execute` if a USART3 command is pending (`app.c:362-363`) | replies `Z` |

One pass therefore takes at least 13.3 ms, plus the render and any runner/flash work. The measured reply time for `:n00=$` → `:Rev201` is a median of 22–23 ms (original: 27–30 ms) (`build/latency-ours.json`, BACKLOG.md:8-10). The smallest observed time, about 5 ms, is roughly the USB + UART floor.

### A.3 Interrupts (all priority 0, so nothing preempts anything)

| IRQ | Handler | What it does |
|---|---|---|
| TIM3, 10 kHz | `board.c:155-163` → `tick_isr_hook` in `app.c:17-42` | `g_ticks`/`g_tick8`; 16-step soft PWM for PB7 and the PB12 backlight (625 Hz); channel `elapsed++` every **101** ticks = 10.1 ms (1 % long, as in the original, kept); key-hold counter +1 every 25.6 ms; marquee offset +1 every 25.6 ms, and in PC mode it toggles which name scrolls (`UIF_PC_NAME_G2`) |
| USART1/2 RX | `uart.c:46-69` → `uart_rx_hook` → `link_rx` (`link.c:268-337`) | Byte-by-byte parser. `:g`/`:p` values go **directly into `g_ch[ch].params` / `g_ch[ch].prog`** (`link.c:99-114`). The mirror commands w11/14/15/24/25 are applied inside the interrupt (`link.c:160-224`). A complete line sets `UIF_LINE(ch)`; from then on the link **drops bytes** until the main loop has handled it (`link.c:284-285`). The `:ok` matcher runs even while a line is pending (`link.c:275-282`). |
| USART3 RX | `factory.c:29-60` | Parses `:a..l<n>\n` and sets `g_factory_cmd_flags` bit 0 |
| Not used | SysTick, DMA, IWDG/WWDG, EXTI | no watchdog anywhere (hardware.md §4.1) |

Receive errors are cleared (`uart.c:52-55`). The USART FIFOs are not enabled (`uart.c:28`), so the interrupt must be served within about 1–2 byte times (87–174 µs).

### A.4 Shared state (`state.h`)
- One set of globals that mirror the original's RAM:
  - `g_ch[2]` (`state.h:76-87`): program record `prog` (0x668 bytes, same layout as flash), `params` (u16[400]), `prog_slot`, `step`, `step_sent` (0xFF forces a resend), `step_freq`, `elapsed` (volatile, 10.1 ms units; 0 = fresh, 1 = stopped), `pc_freq[2]`.
  - Flag words `g_ui_flags`, `g_pc_flags`, `g_misc_flags`, `g_factory_cmd_flags` (`state.h:22-54`).
  - `g_ui_state` (BOOT 0, HOME 1, LIST 2, CH1 3, CH2 4, PC 0x10, CAL 0xFF; `state.h:12-20`).
  - Key, list and calibration variables; `g_names[32][60]`; `g_name_bmp[1998]`.
- The main loop must change flag words with `flags_set`/`flags_clear`, which briefly disable interrupts (`state.h:117-136`).
- `UIF_PC_UNLOCK` is always set (`state.c:10`), so the access gate is always open.

### A.5 Module boundaries

```
main_fw ─► app (loop, keys, state machine, PC entry/exit, CAL keys, power off)
            ├─► runner   (program stepping → link_send_* + link_wait_ok)
            ├─► link     (U1/U2 RX parser in the interrupt, replies, maintenance w-commands)
            │     ├─► storage (SPI-flash layout) ─► spiflash
            │     ├─► factory (USART3)  ── writes g_ui_state and draws via ui_cal_draw_*
            │     └─► calib   (CAL test-signal command tables)
            ├─► ui (screens) ─► gfx (primitives, fonts, marquee) ─► lcd (ST7789/SPI2) ─► assets (vendor, generated)
            └─► board / keys / uart / fmt
```

- The Makefile groups sources by prefix (`Makefile:31-41`).
- Host tests cover link, factory, calib, storage and spiflash without any UI (`tests/host/Makefile`, 19017 checks).
- The UI's only "outputs" into application state are bookkeeping: `UIF_REDRAW`, `g_ui_prev_state`, marquee variables, and the PCF "up to date" bits (`ui.h:1-5`).

### A.6 Runner (`runner.c`)
- Step number: `step = elapsed / (dwell*100) + 1`, with dwell 0 treated as 1 (`runner.c:7-17`).
- Per channel, G1 block first, then G2:
  - **Program finished** (`step > count`): without loop, send `:w11=0,0,` if it was running and clear RUN. Then `elapsed = 1`, and redraw unless in the list (`runner.c:80-88`).
  - **New step** (`step != step_sent`): `send_step`. It replaces 0xFFFF in the step's (a,b) pair with 0 **in RAM** (`runner.c:39-43`), then sends one of two sequences, waiting for the ack after each command:
    - **Idle** (not running), 10 commands: `w24 f`, `w11 0,0`, `w14 1`, `w17 0,1`, `w20/w21 hdr0`, `w28/w29 hdr1`, `w32/w33 hdr2`.
    - **Playing**: `w24 f`, `w11 1,1`, then `w12 0,0` if the pair is (0,0), otherwise `w50`/`w51` pair5 + `w12 1,1` (`runner.c:45-67`).
  - A redraw is requested unless the state is LIST or PC (`runner.c:68-69`).
- Keys only change flags, `elapsed` and `step_sent`; the runner turns those into generator traffic on the next pass.

---

## B. Current UI/UX inventory

Logical landscape coordinates are X = 319 − y_phys, Y = x_phys (display.md §5). All drawing is opaque 1 bpp with no frame buffer. Colours: TEAL 0x2B12, GREY 0x8430, WHITE, BLACK, YELLOW 0xFFE0, RED 0xF800, NAVY 0x0016 (`lcd.h:14-20`, `ui.c:14-20`).

### B.1 Screens

| Screen / state | Trigger | Content and layout (logical) | Assets / fonts | Code |
|---|---|---|---|---|
| Boot splash (before the loop) | power-on | black background; white ghost "Spooky" logo (X120–199, Y30–141) with a red "2"; "Welcome to Spooky" (X58–269, Y173–196) with a large red "2"; tagline "Professional • Friendly • Effective" (X78–259, Y200–215); about 4 s | bmp_boot_ghost_logo, red2_small, boot_welcome, red2_large, boot_tagline | `ui.c:31-38`, `app.c:318-341` |
| BOOT (0) | reset, or factory `:a1` | nothing; next pass goes to HOME, or PC if PB1 is high | – | `app.c:260-263` |
| HOME (1) | boot, or leaving PC mode | Two stacked panels. G1: TEAL title bar Y20–44, GREY body Y45–114, X19–299. G2 the same at Y130–224. White badge discs (r=15) with navy "G1"/"G2" at X≈25. "Choose Your Treatment Program" in both title bars. The PC icon is erased. | font_ui24, font_gnum_16x22 | `ui.c:420-426`, `ui.c:108-118` |
| LIST (2) | G1/G2 from HOME or a channel screen | White rounded box X19–299, Y40–215. Header "Program  G1"/"Program  G2" (white on black, Y15–38). 7 rows of 25 px: "NN." plus the name in a 230 px marquee window (X54–283). Selected row: TEAL bar, white text; others black on white. Only the selected name scrolls. | font_ui24 | `ui.c:226-286`, `ui.c:449-450` |
| CH1 (3) / CH2 (4) | START/STOP in the list, or G-key switching | Active panel: white body with TEAL border and a TEAL ring round the badge. The other panel stays GREY. Title shows the program name (marquee on the active one) or "Choose…". For a loaded channel: status icon 48×51 on the right (X239–289; 'c' Ready, 'r' Running, 'p' Paused, 's' Stopped) and the frequency "IIIIIIII.FFFFFFFF Hz" on the left (X29→, 8 decimals, shown only while running, otherwise 0). Loop badge 16×16 at the right end of the title bar (YELLOW if loop is on, otherwise invisible TEAL on TEAL). Inactive panel uses GREY background and white text. | font_ui24, font_freq24, icons_status_48x51, bmp_sync_icon, font_gnum | `ui.c:120-222` |
| PC (0x10) | PB1 high | Both panels "active". Title bars show the PC names (slot 0 = G1, slot 31 = G2), and the scrolling name alternates. "Out1"/"Out2" labels and 4 frequencies (TEAL if the output is on per `w11`, else GREY; Out2 follows Out1 in sync mode per `w14`). White USB/PC icon at X282–314, Y4–19. **No key handling** apart from power-off. | font_out_24x44, bmp_usb_icon, font_freq24, font_ui24 | `ui.c:288-355` |
| CAL (0xFF) | G1+G2 at boot, or factory `:a0` | 29 five-digit calibration values, white on black, in a fixed grid; the selected one is drawn inverted | font_ui24 (digits) | `ui.c:357-407` |
| Goodbye (terminal) | POWER held about 0.8 s | Backlight off, clear, draw, backlight on. QR code (spooky2.com), small "Spooky" logo with red "2", "Looking forward to seeing you again!", "www.spooky2.com". Then 4 s, then the PB2 square wave forever. | bmp_bye_qrcode, bye_logo, bye_text, bye_url, red2_small | `ui.c:40-48`, `app.c:289-297`, `board.c:183-191` |
| Error screens | – | **None.** Flash failures, missing generator acks and `:err` are silently ignored (`link.c:444-450`, `storage.c:50-51, 108-109`). | | |

### B.2 Key model (`app.c:44-85`, application.md §3.2)
- **Scan:** 13.3 ms busy delay, then a read in fixed priority order: G1+G2 > POWER+STOP > LOOP > START > STOP > UP > DOWN > G1 > G2 > POWER (`keys.c:5-31`).
- **Press** is reported for one pass, after two consecutive scans. **Held** = code | 0x10. **Release** = | 0x20 (short) or | 0x40 (long, ≥16×25.6 ms); release events are never used.
- **Hold counter** increments every 25.6 ms while a key is held (`app.c:27-30`).
- **Auto-repeat:**
  - List UP/DOWN: hold > 10 (≈0.28 s) and `tick8 < 100` (`app.c:77-80`).
  - CAL G1/G2: hold > 9 and `tick8 > 99` (`app.c:82-85`).
- **Long press:** only POWER, > 30 (≈0.8 s) → power off, in every state including PC and CAL (`app.c:351-352`).
- PB6 and PB10 are pulled-up inputs that are never read (possibly unpopulated keys).

### B.3 Key map per screen

| Key | HOME | LIST | CH1 / CH2 (ch = this channel) | PC | CAL |
|---|---|---|---|---|---|
| G1 | → LIST(G1) | target = G1 (stay) | CH1: → LIST(G1). CH2: → CH1 if G1 has a program, else LIST(G1) | – | value +1 (−1 for the 3rd/4th value of each group of five), auto-repeat, sends `:w60` |
| G2 | → LIST(G2) | target = G2 | mirror of G1 | – | value −1 (inverted likewise), sends `:w60` |
| UP / DOWN | – | selection −1 / +1 (clamped 1..30, window of 7), auto-repeat | – | – | index −1 / +1 (wraps 0..28) and sends the test signal |
| START | – | load the selected slot into the target channel (clears RUN, keeps LOOP, elapsed 0, both `step_sent` = 0xFF) → CH(target) | not running: play (loop cleared); running: pause | – | save; index 26 saves with backup; sends `:w61` |
| STOP | – | → CH(target) without loading | stop: elapsed = 1, RUN and LOOP cleared | – | cycle test frequency exponent (1..9) |
| LOOP | – | – | not running: play with loop; running: pause, loop set | – | cycle test amplitude exponent (4..10) |
| POWER (short) | – | – | **stop both** channels (loop flags kept). This also fires at the start of every long press, so switching off from a channel screen first sends the idle configuration to both generators. | – | – |
| POWER held > 0.8 s | power off | power off | power off | power off | power off |
| G1+G2 at power-on | – | – | – | – | enters CAL. There is **no key exit** from CAL. |

References: `app.c:137-255`. POWER+STOP (code 10) is unused by the app; it is the bootloader combination at power-on.

### B.4 Navigation (text state diagram)
```
power-on ─► SPLASH (≈4 s, cut short by a USART3 frame)
   ├─[G1+G2 held]─► CAL ──(USART3 :a1)──► BOOT
   └─► BOOT ─(1 pass)─► HOME            (PB1=1: ─► PC instead)
HOME ─G1/G2─► LIST(t)
LIST ─START─► CH(t) [load]   ─STOP─► CH(t) [no load]   ─G1/G2─► LIST(other t)
CH1 ─G1─► LIST(G1)   ─G2─► CH2 if loaded, else LIST(G2)      (CH2 mirrors this)
ANY ─PB1↑─► PC (entry sequence)   PC ─PB1↓─► HOME (exit sequence)
ANY ─USART3 :a0─► CAL      ANY ─POWER hold─► GOODBYE ─► PB2 square wave (terminal)
PC or ANY ─ :w95=12021, ─► MCU reset
```
The runner keeps running in HOME, LIST and CH states, and also in BOOT for one pass (`app.c:355`).

### B.5 Vendor assets (extracted at build time by `tools/gen_assets.py:25-45`)
All of these need open replacements in a new UI. Their total size in flash is about 11.5 KB (ELF symbol sizes). The only UI strings in code are "Choose Your Treatment Program" and "Program  G1/G2" (`ui.c:25-27`).

| Asset | Used for | Replacement needed |
|---|---|---|
| `font_ui24` (86 proportional glyphs, 24 px; **no** `" ; < > ? \ \` \| *` and no non-ASCII) | names, list, titles, CAL numbers; also `lcd.c:201-247` (`gfx_text` of the bring-up/linktest apps) | an open bitmap font covering at least ASCII 0x20–0x7E. Names are raw bytes: `:n?` returns bytes 0x20..0x7F only (`link.c:518-523`); rendering skips unknown codes (`ui.c:66-68`). |
| `font_freq24` (digits ( ) - SP . , H z) | frequency readout | open digits font |
| `font_gnum_16x22` ("G1", "G2") | channel badges | text in the new font |
| `font_out_24x44` ("Out1", "Out2") | PC screen labels | text |
| `icons_status_48x51` (c/r/p/s with text) | channel status | own icons (can be drawn in code) |
| `bmp_usb_icon`, `bmp_sync_icon` | PC icon, loop badge | own icons |
| `bmp_boot_*`, `bmp_red2_*` | splash (Spooky2 branding/trademark) | own splash |
| `bmp_bye_*` (QR to spooky2.com, logo, text, URL) | goodbye screen | own screen |
| `bmp_unused_waveforms` | nothing (removed by `--gc-sections`) | – |

Removing every asset use (`gfx.c:9`, `ui.c:9`, `lcd.c:3`) would also remove the build's dependency on `original/GxDisplayFW.bin` (`Makefile:51-55`). Anyone could then build the firmware without the vendor binary.

---

## C. Protocol boundary

### C.1 Wire-visible to the PC: must stay 1:1

**1. Line parser** (`link.c:268-328`, protocols.md §2.2):
- Only the letters `w p g n` are accepted; the two number characters are taken as `c & 0x0F`; `=` is required.
- `,` or `.` ends a value; an empty value means all ones; the token before CR LF is ignored.
- A token of more than 26 characters drops the line; `n` text of more than 59 characters drops the line; CR in `n` text becomes NUL.
- Index clamps: g 399, p 204, w 31, and further values overwrite the last element (`link.c:112-113`). `g` values wrap to 16 bits.
- **Value-index carry-over:** the index is reset only after a frame is handled (`link.c:305-320`, `222`, `690`), and `wargs` keep stale values.

**2. Byte drop while a line is pending** (`link.c:284-285`). The PC has to wait for each reply.

**3. Which `w` numbers are handled at once in the interrupt and which wait for the main loop** (`link.c:160-224`):
- Deferred to the main loop: 0, 16, 60 (U2), 62 (U1), 63, 64, 80, 91, 92, 95, 96, 97.
- Mirrored in the interrupt: 11, 14, 15, 24, 25.
- Everything else is dropped in the interrupt.

Moving a number from "at once" to "deferred" widens the byte-drop window and is therefore wire-visible.

**4. Replies** (`link.c:455-569`):

| Frame | Reply |
|---|---|
| `g`, `p`, and `n` sub-commands `,` and text | `:ok\r\n` or `:err\r\n` |
| `:nNN=$` | `:Rev201\r\n` |
| `:nNN=?` | `:nNN=<name>` (up to 57 bytes, stops at a byte < 0x20 or > 0x7F). `NN` is echoed as `num\|0x30` (`link.c:465-473`). |
| `:nNN=*` | `:pNN=` + header + `count` frequencies as `v,` each, only if 1 ≤ count ≤ 200, then CR LF (`link.c:476-492`) |
| `:nNN=#` | `:gNN=` + **the first 200** u16 values, only if `g[0] != 0xFFFF` (`link.c:497-513`) |
| any `w` frame | no reply from the display |

**5. Reply conditions and ordering:**
- `g`/`p` slot must be 1..30, `n` slot at most 30, otherwise `:err`.
- On U2, `n` slot 00 means slot 31 (`link.c:536-537`).
- `,` erases the slot's sector but **keeps the name** (`link.c:552-556`).
- `:ok` is sent **after** the flash write for `p` and for name uploads (`link.c:561-565`, `674-681`).

**6. Upload side effects** (visible through later replies):
- Uploads write into the channel's active buffers in the interrupt, even when the reply is `:err`.
- `:p` stores whatever `params` currently hold (`storage.c:110-111`).
- The conformance case "parser-trailing-value-dropped" shows a stale `freq[1] = 10008` from an earlier upload surviving into the new program.

**7. Maintenance commands** with their magic numbers (`link.c:571-655`): w00 (acknowledged, no effect), w60 (U2), w62 (U1), w63, w64 on/off, w80, w91 (PB2 low), **w92** (2 × 13.3 ms delay, PB2 high, `:w91=0,` sent on the same port), w95 reset, w96 (once per power cycle), w97.

**8. PC-mode entry and exit** (`app.c:104-135`):
- **Entry:** PB2 high, state PC, clear RUN/LOOP, `elapsed` and `prog_slot` = 0, wait 13.3 ms, `:w11=0,0,` + ack on U1 then U2, wait 13.3 ms, PB2 low.
- **Exit:** PB2 high (and it stays high), `:w15=0,0,` + ack on both links, clear the mirror state, go to HOME.

**9. Factory port (USART3):** `:a` through `:l`, one byte `Z` in reply (`factory.c:91-154`), and the U1 messages it triggers (`:w60`, `:w61`).

**10. "Kept on purpose" list** (DEVIATIONS.md:82-93): `:n#` sends 200 values; w95 resets; the 13.3 ms main-loop pacing; the value-index carry-over.

**11. Deliberate deviations the PC relies on:** the access gate is always open; `w91` accepts any value.

**Authoritative tests:** `make conformance` runs 40 cases on the real device with an authenticated session, including the w92 handshake (`build/conf-ours.json`, `Makefile:76-88`). `tools/diff.py` compares UART lines, PB2 levels and flash operations separately from screen pixels (`tools/diff.py:1-30`). Screen checkpoints can be marked `informational` while the serial comparisons stay strict.

### C.2 Wire-visible to the generator boards only (standalone and CAL)
The PC does not see this traffic (in standalone no PC is attached; during PC-mode entry PB2 high isolates the PC, hardware.md:475).
- **Program runner sequences, boot idle configuration, stop/pause/end sequences.** These are confirmed on the device. They can be **extended** for new features within what the generators accept. Existing flows are better left unchanged unless you intend to change them.
- **Calibration** (`:r80`, test tables `calib.c:24-76`, `:w60`/`:w61`): treat as frozen, since it is a service procedure.
- **Timing:** the dwell runs 1 % long (`app.c:19-20`), the ack wait is shared between links (`link.c:63`), and ack timeouts are ignored. Changes here affect generator timing only.

### C.3 Persistent format: SPI flash, W25Q16, 2 MB

| Area | Content | Validity rule | Written by |
|---|---|---|---|
| 0x005000 (whole 4 KB sector) | names 32 × 60 bytes, NUL padded; slots 0 and 31 always stored as zeros; 0xFF read as 0 | – | `names_save` erases and rewrites the **whole sector** (`storage.c:47-55`) |
| 0x010000 + n × 0x1000, n = 1..30 | record: u64 hdr[5] (wave, ampl in 0.01 V, offset 120, dwell s, count) + u64 freq[200] at +0; u16 params[400] at +0x668 (0xFFFF = none) | count 1..200 and dwell ≠ 0, otherwise the defaults {0,0,120,1,1} (`storage.c:90-97`) | `program_save` erases the sector (`storage.c:101-112`) |
| 0x074000 / 0x075000 | calibration s16[32] (working copy / backup) | word[31] == 31 | `cal_save` / `cal_restore_backup` (`storage.c:132-172`) |

The layout is enforced by static asserts (`storage.c:24-27`) and validated byte for byte against a real backup (application.md §13: only these sectors are used).

**Free areas:** 0x00000–0x04FFF, 0x06000–0x0FFFF, 0x30000–0x73FFF (68 sectors) and 0x76000–0x1FFFFF (about 1.5 MB). Avoid the following:
- **0x10000 (slot 0) and 0x2F000 (slot 31).** The original firmware erases these on `:n00=,` (U1 and U2 respectively; DEVIATIONS "program_save/program_erase"). If the original is ever flashed back, data there is lost.
- **The unused tails** of the names sector, the program-slot sectors and the calibration sectors. Every save erases the whole sector.
  - A program sector's tail (0x988–0xFFF) could hold per-slot metadata *only if* you accept that every PC upload, `:n,` and `w96` wipes it.
- **Erase-all interaction.** `w96` erases only slots 1..30 and the names sector (`storage.c:122-128`). New data such as favourites that point at slots must cope with the programs disappearing.
- **Internal flash.** It is not suitable (the bootloader may erase it; the 0x08003xxx page is off-limits).

### C.4 Purely local (free to redesign)
- All of ui.c, gfx.c and lcd.c drawing: layout, fonts, colours, orientation (MADCTL may change; the panel-variant init lists must stay).
- Marquee logic and TIM3 marquee state; list and highlight variables; `UIF_LIST_*`; `g_ui_prev_state`.
- Key mapping outside CAL (key semantics in CH states only affect *when* the runner sends); key debounce and repeat timing, **if** the pacing is decoupled from link handling (see C.5).
- Splash and goodbye content and splash length (a shorter splash only brings the first idle configuration and PC detection earlier).
- Backlight level (16-step PWM); the `w64` on/off meaning must stay.
- New status information (step N/M, remaining time, amplitude, waveform); new error screens.

### C.5 Subtle couplings
1. **Loop pacing affects latency and dropped frames.** Replies are sent only in step 6 of a pass. While a *deferred* `w` frame is pending, the link drops bytes (`link.c:284`). The pass length therefore decides both reply latency and which back-to-back PC frames the display misses. A faster loop only accepts more frames and replies sooner, but that is still wire-visible timing: re-run `make conformance`.
2. **Runner before link handling** (`app.c:355-360`). In standalone, a pending reply waits behind a whole runner sequence. The PC is not affected, because the runner is off in PC mode.
3. **Render before the factory reply** (`app.c:361-363`). The `Z` reply is late by the redraw time (DEVIATIONS: 62 ms after one redraw).
4. **Order across links.** U1 is always processed before U2 within a pass (`app.c:357-360`). Within one link, order is preserved by construction (one pending frame, the rest dropped).
5. **w92 blocks the main loop for 26.7 ms** inside `link_process`, then raises PB2 and sends `:w91=0,` (`link.c:624-629`). This is part of the vendor session establishment; keep its timing and position exactly.
6. **PC-mode entry blocks** for about 27 ms plus up to 2 × 40 ms with PB2 high, which isolates the PC. The first Spooky2 frames after USB connects may land in this window, exactly as with the original.
7. **Active program buffer = upload receive buffer** (`link.c:99-114`, `storage.c:110-111`, `runner.c:39-43`):
   - The runner rewrites 0xFFFF → 0 in `params`, and a later `:p` without `:g` saves that.
   - After leaving PC mode, the runner sends an idle configuration built from the *last uploaded* buffer.

   A new UI must not use `g_ch[].prog`/`params` as scratch space for previews or editing. Use separate buffers.
8. **PC mode does not depend on the screen.** `link.c` never reads `g_ui_state` (grep). `ST_PC` is used only for PB1 edge detection (`app.c:107/123`), runner gating (`app.c:355`) and redraw suppression (`runner.c:68`). A new UI may show anything in PC mode, but must **not transmit on U1/U2 there** (PB2 is low, so the line is shared with the PC) and must not let keys cause generator traffic.
9. **The factory port drives the UI.** `:a0`/`:a1` write `g_ui_state` and clear or draw the screen directly; `:e..:l` draw a calibration value in *any* state (`factory.c:99-111, 144`). The new UI must provide `ui_cal_draw_values`/`ui_cal_draw_value` and handle re-entry through BOOT.
10. **TIM3 hook does two jobs.** Program timing (`elapsed`) and the marquee live in the same hook (`app.c:17-42`). Replacing or re-rating the hook changes generator step timing.
11. **Interrupt budget.** All interrupts are priority 0 and the FIFOs are off. Long interrupt-disabled sections or heavy work in TIM3 would cause UART overruns → dropped frames → the PC times out.
12. **New sniffing must stay in the interrupt.** If the UI sniffs more PC traffic (for example `w20/w21/w28/w29`, or generator `:r11=`/`:r12=` replies for a biofeedback view), it must be handled in the interrupt and cleared at once, never deferred (see point 1). Today `r` lines are ignored (`link.c:293-298`).
13. **Extension commands.** New display-only commands must not use `:n` sub-characters (any other first character is stored as a name) or `:w` numbers. Lines starting with `#` are ignored by the generator boards (hardware.md §9, probe app), so a `#…` channel is safe for new tooling.

---

## D. What the display sends to the generators, and whether a session is needed

**Builders** (`link.c:397-436`): `link_send_line` (literal + CR LF), `link_send_w_u64` (`:wNN=<dec>,`), `link_send_w_pair5` (`:wNN=aaaaa,bbbbb,`), `link_send_cal_value`. Sending blocks until the transmission is complete. `link_wait_ok` waits up to 40 ms for `:ok` on **either** link (shared flag), ignores `:err` and timeouts, then waits another ~0.13 ms (`link.c:441-450`).

| When | Link | Sequence |
|---|---|---|
| First pass after boot (HOME) | A, then B | idle configuration of the empty program: `:w24=0,` `:w11=0,0,` `:w14=1,` `:w17=0,1,` `:w20=0,` `:w21=0,` `:w28=0,` `:w29=0,` `:w32=120,` `:w33=120,` |
| LIST START (load) | both (both `step_sent` = 0xFF) | the target channel's idle configuration for step 1; **the other channel also resends its current step** (the running sequence if it is playing) |
| START / LOOP → play; new step while playing | channel | `:w24=f,` `:w11=1,1,` then `:w12=0,0,` or `:w50=a,b,` `:w51=a,b,` `:w12=1,1,` |
| Pause, STOP, POWER-short (both channels) | channel(s) | idle configuration of the current step (STOP: step 1) |
| End without loop | channel | `:w11=0,0,`, then on the next pass the idle configuration for step 1 (if count > 1) |
| End with loop | channel | running sequence for step 1 (if count > 1) |
| PC entry / exit | A, then B | `:w11=0,0,` (PB2 pulse) / `:w15=0,0,`; after exit the runner resends idle from the buffers |
| CAL | A / A+B | `:r80=0,`, test sets per index, `:w60=1112111,II,VVVVV,`, `:w61=111x111,` |

**Cost on the wire:** about 1.5 ms per command at 115200 baud, plus the ack (about 2 ms per the Spooky2 capture) → an idle configuration is roughly 30–35 ms per channel. **If the generator does not acknowledge, each command costs 40 ms**, about 0.4 s per channel. The main loop, and so key handling, is blocked for that whole time.

**Generator registers known to be accepted** (inside a session, per the conformance run, `build/conf-ours.json`): w11 (including single-field `:w11=1,,`), w12 (including per output), w13, w14, w15, w17, w20/w21 (waveform), w24/**w25** (Out1/Out2 frequency), w28/w29 (amplitude), w32/w33, w36/w37, w40, w50/w51, r02, r11/r12, r80. The display never sends w25 or `w14=0` in standalone; the runner always forces sync with `w14=1`.

**Session / access gate:**
- The display itself needs **no session** and talks to the generators freely. There is no handshake code in this firmware: `w91` only drives PB2 low, `w92` only drives PB2 high and sends `:w91=0,`, and the runner starts sending `:w` frames on the first loop pass.
- Standalone program running is confirmed on the device (DEVIATIONS.md:12).
- The documented generator gate (`:err` without a session) was observed **from the PC side, in PC mode** (serial-commands.md:122-137, BACKLOG.md:34-35).
- **Not recorded anywhere:** whether the generators send `:ok` for display-originated commands in standalone, and whether they answer display-originated `:r11`/`:r12`/`:r02`. The display would work either way, because it ignores timeouts.
- **Verify before building timing-sensitive features.** Add a local counter of `gen_ok` hits vs timeouts in `link_wait_ok`, or probe the line. The answer sets the timing budget for sweeps and decides whether local reads (biofeedback) are possible at all.

**New standalone features feasible without generator changes:**
- **Manual frequency / amplitude / waveform mode:** `w24` (+ `w25` with `w14=0`), `w28`/`w29`, `w20`/`w21`, `w11`. Waveform codes are only partly understood (programs contain 41–48; Spooky2 also uses 11 and 25), so map them empirically.
- **Sweeps:** a `w24` per step at about 3.5 ms (with acks), i.e. up to ~250 steps/s. Changing w28/w20 **while playing** is untested; the original sends those only in the idle configuration.
- **Timers, countdown, sleep timer** (stop = idle configuration).
- **Playlists and favourites:** local metadata in a new sector.
- **More local program slots:** the PC can address only 1..30.
- **Richer PC-mode monitoring** by sniffing, per C.5 point 12.
- Constraints: never transmit in PC mode; use separate RAM buffers (C.5 point 7); keep the frequency encoding v = m·10 + e, Hz = m·10^(e−8) (`gfx.c:184-186`).

---

## E. Optimisation opportunities

| # | Opportunity | Where | Estimated gain | Visibility |
|---|---|---|---|---|
| 1 | Handle pending link lines during the debounce (BACKLOG idea), or replace the busy debounce with key sampling in TIM3 and a non-blocking loop | `app.c:48-52, 349-364` | display replies from a 22 ms median down to about 5–8 ms (firmware part ~2–3 ms); smaller byte-drop window | **PC timing only** (faster, fewer drops). Revises a "Kept on purpose" item. Keep the w92 and PC-entry delays. Re-run `make conformance`. |
| 2 | Non-blocking runner transmit queue with a per-link ack flag (today one flag is shared by both links); G1 and G2 in parallel | `runner.c:19-70`, `link.c:63, 444-450` | UI no longer freezes during sequences (30–400 ms per channel); both channels in about half the time | generator timing only; command order per link unchanged |
| 3 | Stop redrawing all 7 list rows on every UP/DOWN; redraw the 2 changed rows unless the window scrolls; cache name bitmaps | `ui.c:269-286, 254-267` | about 105 KB → about 30 KB of SPI traffic per step (≈3×) | local |
| 4 | LCD: 16-bit SPI frames, DMA (SPI2_TX via DMAMUX) for fills and line-buffered blits, one CS period per window setup, inline pixel streaming | `lcd.c:53-106, 149-198`, `gfx.c:261-265` | Draws are close to the 32 MHz bus limit already (full screen ≥38 ms). Main gain: the CPU is free during transfers (about 3 ms per marquee step, ~40 ms per full screen), plus about 1.2–1.5× for CPU-bound blits in gfx.c. | local. The emulator has no DMA model (`tools/sim/periph.py`, unmodelled peripherals are only listed). |
| 5 | Marquee: avoid re-rendering the glyphs every 25.6 ms | `ui.c:449`, `ui.c:52-86` | small CPU saving | local |
| 6 | RAM: the UART receive ring buffers are unused in the fw build because `uart_rx_hook` always consumes the byte | `uart.c:5-13`, `link.c:330-337` | −1548 B. Today: 11.3 KB of 36 KB used, about 23 KB free after the 2 KB stack. | local |
| 7 | Flash: 31.4 KB of 112 KB used; vendor assets about 11.5 KB | `build/fw` size | about 80 KB free for open fonts, icons and features. The vendor bootloader was only proven with images up to 74.7 KB (the original's size); since the size test of 2026-09-25 it is proven up to 112,640 bytes. | local |
| 8 | Watchdog (IWDG, started in software; option bytes untouched) | none today | recovery from hangs | local. `power_off_signal` must refresh it (`board.c:183-191`), as must long SPI erases. Not modelled in the emulator. |
| 9 | Bound the unbounded busy-waits | `uart.c:98, 112`, `lcd.c:55, 62` | robustness | local |
| 10 | Enable the USART FIFOs (8 bytes on the G0) | `uart.c:28` | more slack in interrupt latency for heavier UI work | local (no wire change) |
| 11 | Fix the 1 % long dwell | `app.c:19-20` | exact treatment durations | generator timing only; document as a deviation |
| 12 | Shorter boot (4 s splash + ~250 ms of LCD delays) | `app.c:338-341`, `main_fw.c:21`, `lcd.c:128-143` | seconds | local; shifts the first idle configuration and PC detection earlier |
| 13 | Do not reply before a flash write finishes (names_save and program_save erase a 4 KB sector) | `link.c:561-565, 678-680` | – | **keep**: `:ok` after persisting is PC-visible |

---

## F. Reusable building blocks and their limits

| Block | API | Limits |
|---|---|---|
| lcd.c | `lcd_init` (panel variant A/B), `lcd_fill_rect`, `lcd_blit1` (opaque 1 bpp), `lcd_stream_begin/pixel/end`; landscape helpers `gfx_fill`, `gfx_text`, `gfx_text_width` (`lcd.c:221-247`, used by the bring-up/linktest apps) | physical coordinates; no clipping, no transparency, no alpha/anti-aliasing; polled, one function call per pixel; the landscape helpers use the vendor `font_ui24` only; `find_glyph` duplicates `gfx_glyph_ui24` |
| gfx.c | circle, half-rounded and rounded rectangles (fixed r=8 corners, 2 px border, original quirks kept), ui24/freq24 text, `gfx_uint5_ui24`, `gfx_frequency` (u64 encoded → "IIIIIIII.FFFFFFFF Hz"), label/icon blits, `gfx_marquee_draw` (fixed 230×24 window) | tied to the vendor asset formats and pixel-exact quirks; linear glyph search; fixed sizes |
| ui.c | `ui_name_render_bitmap` (name → column bitmap), screen routines | pixel copies of the original; `main_uitest.c` depends on them and on the `struct channel` offsets (`main_uitest.c:21-25`) |
| fmt.c | `fmt_uint` (u32), `fmt_hex`, `fmt_str` | no u64, no fixed-point or frequency formatter. u64 decimal (`put_dec`) and fixed-width (`put_fixed`) exist only as `static` in `link.c:364-386`; the frequency decode is embedded in `gfx.c:184-191`. Factor these into fmt. |
| board | `g_ticks` (100 µs), `g_tick8`, `delay_ms`, `delay_loop`, `g_backlight_duty` 0..16 (free brightness control), `power_off_signal` | 10 kHz TIM3 hook shared with program timing |
| keys | `keys_read` (instant, prioritised), `key_name` | two-key combinations limited to G1+G2 and POWER+STOP |
| storage | `names_*`, `program_load/save/erase`, `cal_*`, `storage_slot_read` (`link_internal.h:18-19`: read a slot header, 40 bytes, without loading it, for list metadata such as steps or duration) | whole-sector rewrite on every save; no journaling |
| spiflash | read / erase / write, bounded busy waits | polled |
| Test infrastructure | `tools/emu.py` (LCD → PNG, keys, serial); `tools/diff.py` (UART, PB2 and flash strict; screens can be informational); `tests/host` (protocol and storage, UI-free); `make conformance` (real device) | emulator lacks DMA and IWDG models |

---

## Doc inconsistencies and open items
- **application.md §13** says programs whose step parameters are (0,0) would send `:w50/:w51` + `:w12=1,1,`. The code sends `:w12=0,0,` for (0,0) (`runner.c:58-59`, matching §6.2). The code is right; the doc statement is wrong.
- DEVIATIONS.md says "12/12 scenarios", but `tools/scenarios/` has 13 JSON files. There are 38 `DEVIATION` marks (37 in `.c` files, 1 in `state.h:113`), which matches the doc.
- The PB2 level in standalone depends on history: low after a cold boot (`board.c:84`), high after leaving PC mode or after CAL (`app.c:125, 343`). This has no functional effect without a PC attached.
- **To verify on the device:** whether the generators acknowledge the display's standalone commands and answer its `:r` reads (see D).
