# GX Pro USB-serial commands

What can be sent to a GeneratorX Pro over its USB connection, who answers, and what each command
does. Sources: the reverse-engineered display firmware v7789 ([`spec/protocols.md`](spec/protocols.md),
[`spec/application.md`](spec/application.md)), serial captures of Spooky2, and tests on a real unit.
Meanings marked *(inferred)* come from how the commands are used, not from the generator firmware,
which was not analysed.

## 1. Connection

| | |
|---|---|
| USB device | WCH CH342 "USB Dual_Serial": two serial channels |
| Settings | 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control |
| Channel → link | `…903` (e.g. `/dev/cu.wchusbserial…903`) = **link A** = generator A (G1) + display USART1 |
| | `…901` = **link B** = generator B (G2) + display USART2 |
| PC mode | With USB connected (display input PB1 high) the display enters PC mode and gives the links to the PC (PB2 low). The PC then reaches the generator board **and** the display on the same link; the display listens to everything and answers only its own commands. |

Each link is shared: commands for the generator board are answered by the generator, commands for
the display (`g`, `p`, `n` and a few `w` numbers) by the display. The display never answers `w`
commands itself.

## 2. Frame format

```
:<letter><NN>=<value>,<value>,...,<CR><LF>
```

- `<letter>`: `w` set/write, `r` read, `p` program upload, `g` step-parameter upload, `n` name/query.
- `<NN>`: exactly two digits: command number for `w`/`r`, program slot for `p`/`g`/`n`.
- **Every value ends with a comma** (a `.` works too). A value before `CR LF` without a comma is not evaluated.
- An empty value (`,,`) means "all ones" (used as "no value", e.g. `0xFFFF` in step tables).
- Replies: `:ok` / `:err` (with CR LF), or a data line.

### Number formats

| Value | Encoding | Examples |
|---|---|---|
| Frequency | `v = m·10 + e`, **Hz = m · 10^(e−8)**: the last digit is a power-of-ten exponent. Spooky2 sends up to 8 decimals (12 digits and more) and strips trailing zeros into the exponent | `7836` → 7.83 Hz · `41009` → 41 000 Hz · `3750007` → 37 500 Hz · `123456007` → 1 234 560 Hz · `368` → 36 Hz · `846423193440` → 846.42319344 Hz |
| Amplitude *(inferred)* | 0.01 V | `2000` → 20.00 V |
| Program dwell | seconds | `600` → 10 min (the original and the classic firmware run 1 % long: they count 10.1 ms units; the nx firmware runs exactly) |

## 3. Commands answered by the display

### 3.1 Programs and names (`p`, `g`, `n`)

The device stores **30 programs** (slots `01`–`30`) and 32 names in its external flash. `p`/`g`/`n`
work on either link; programs are shared, but each link fills its own channel's working buffer
(link A → G1, link B → G2).

| Command | Reply | What it does |
|---|---|---|
| `:gNN=a1,b1,a2,b2,…,` | `:ok` | Loads per-step gate parameters (up to 200 pairs a,b, 0–65535; empty = none) into the channel's working buffer. Not stored yet: send it **before** the matching `:p`. |
| `:pNN=h0,h1,h2,h3,h4,f1,f2,…,` | `:ok` (after the flash write) | Loads a program into the channel's working buffer and **stores** it in slot NN together with the step parameters from `:g`. `h0` waveform *(inferred)*, `h1` amplitude, `h2` offset *(inferred, default 120)*, `h3` dwell per step in s, `h4` number of steps (1–200), then `h4` frequencies. |
| `:nNN=<text>` | `:ok` | Sets the name of slot NN (max. 60 characters) and stores all names. Slot `00` is the name shown in PC mode for that link (not stored). |
| `:nNN=?` | `:nNN=<name>` | Reads a name (up to 57 characters). |
| `:nNN=*` | `:pNN=h0,…,h4,f1,…,` | Reads a stored program; the reply has the upload format and can be sent back as-is. |
| `:nNN=#` | `:gNN=v1,…,v200,` | Reads the stored step parameters: **only the first 200 of the 400 values** (original behaviour, kept). |
| `:nNN=,` | `:ok` | Erases program slot NN (the name stays). |
| `:nNN=$` | `:Rev201` | Firmware version. Always answered. |

- Slot outside 1–30 for `g`/`p`: `:err`. `n` accepts `00`–`30` (on link B `00` means the PC name of G2).
- **Original only:** while connected to the PC, all of these except `$` answer `:err` unless the PC
  software has opened a session. OpenGX Pro always accepts them.
- Uploading also replaces the program currently loaded on that channel, even when the reply is `:err`.

### 3.2 `w` commands the display acts on

No reply from the display (the generator board may answer `w11`–`w25`, see §4).

| Command | Link | What the display does |
|---|---|---|
| `:w11=a,b,` | A/B | PC mode: output 1/2 of that channel shown as on (1) or off (0) |
| `:w14=x,` | A/B | PC mode: `1` = output 2 follows output 1 ("sync"), `0` = independent |
| `:w15=a,b,` | A/B | stored only, no visible effect |
| `:w24=f,` | A/B | PC mode: shows f as output-1 frequency (and output 2 in sync mode) |
| `:w25=f,` | A/B | PC mode: shows f as output-2 frequency (ignored in sync mode) |
| `:w64=18888881,` / `:w64=10000001,` | A/B | display backlight on / off |
| `:w63=111999,` / `:w63=111000,` | A/B | calibration value 0 +1 / −1 (range 31900–32100) and store it |
| `:w62=3336333,` | A | restore the calibration from its backup copy in flash |
| `:w60=1112111,II,VVVVV,` | B | set calibration value II to VVVVV (RAM only) |
| `:w80=13583792,c0,…,c20,` | A/B | set calibration values 0–20 (RAM only); the generator's reply to `:r80=0,` |
| `:w96=12321,` | A/B | **erase all 30 programs and all names** (once per power-up) |
| `:w97=12621,` | A/B | PC mode: clear that channel's frequencies and both PC names, sync on |
| `:w92=,` | A/B | sets PB2 high (presumably giving the link to the display, *inferred*), then sends `:w91=0,` on the same link |
| `:w95=12021,` | A/B | restarts the display MCU |
| `:w91=…` | A/B | part of the vendor's PC-session handshake (not documented here); OpenGX Pro accepts any value |
| `:w00=…` | A/B | original: stores a one-time value in the bootloader's flash page; OpenGX Pro: ignored |

## 4. Commands for the generator boards

Answered by the **generator board** on that link, with `:ok` (for a `w` set) or a data line (for an
`r` read). These are what Spooky2 and the display itself send. Confirmed in a Spooky2 capture: after
the session is open, every register write below is answered with `:ok` (times from the capture:
about 2 ms each).

| Command | Meaning |
|---|---|
| `:r02=0,` | **generator firmware version**, reply `:r02=<n>.` (seen: `200`) |
| `:w11=a,b,` | output 1 / output 2 on (1) or off (0); one side may be left empty to change only the other (`:w11=1,,`, `:w11=,1,`) *(inferred)* |
| `:w12=a,b,` | per-step gating on (1) or off (0), with the parameters of `w50`/`w51` *(inferred)*; also `:w12=0,,` / `:w12=,0,` per output |
| `:w13=x,` | sent by Spooky2 before a step and by the calibration menu; meaning unknown |
| `:w14=x,` | output 2 follows output 1 ("sync") *(inferred from the display mirror)* |
| `:w15=a,b,` | sent as `0,0` when PC mode ends and as `1,1` during setup; meaning unknown |
| `:w17=a,b,` | sent as `0,1` in the display's idle configuration, as `0,0` by Spooky2 at connect (it inverts output 2 with table 25 instead); *inferred: inversion of output 1 / 2* |
| `:w20=x,` / `:w21=x,` | wave table of output 1 / 2: 11–19 are Spooky2's waveforms 1–9 (sine, square, sawtooth, inverse sawtooth, triangle, damped sine, damped square, H-Bomb sine, H-Bomb square), 20 and 21 its two user-defined shapes, 25 the inverse of output 1's, 24 a flat one, 41–70 the table of stored slot n − 40 |
| `:aNN=v0,v1,…,v1023,` | **upload wave table NN**: 1024 samples of 0–1023 (512 = the middle), one line of about 4.1 KB, answered `:ok` 40–700 ms after the line. Spooky2 uploads tables 11–21, 24 and 25 at every preset load. With a Wave Cycle Multiplier of n it puts n cycles into the table and sends every frequency divided by n (seen: n = 11, the sine at half scale, `floor(512 + 255.75·sin)`). Its two H-Bomb tables (18, 19; multiplier 16 in the standard shells) are 16 cycle slots of 64 samples: 4 flat at 512, one full-scale cycle, 6 cycles at about 20 %, one full-scale cycle, 4 flat. Its standard sine (no multiplier): one cycle at full scale, `floor(512 + 511.5·sin(2πi/1024))` with 511 at i = 512, a line of 3932 bytes. The nx firmware uploads that table 11 itself before its manual generator or a Terrain set plays the sine, and the two tables of a built-in preset |
| `:w24=f,` / `:w25=f,` | frequency of output 1 / 2 (`:w24=0,` or `:w24=00,` = idle) |
| `:w28=a,` / `:w29=a,` | amplitude of output 1 / 2 in 0.01 V, peak to peak (Spooky2's guides: "Maximum Amplitude: 20 Vpp", resolution 0.01 V). Spooky2 sends what the output can give, not always the preset's setting: 2000 for 20 V without an offset, 1000 for 20 V at a 100 % offset (only half the −10 … +10 V range is left), less below its "Reduce Amplitude" frequency |
| `:w32=x,` / `:w33=x,` | offset of output 1 / 2: 120 + percent (120 = none, the program header's default; seen: 20 for −100 %, 220 for +100 %) |
| `:w36=x,` / `:w37=x,` | calibration jig only (`0`) |
| `:w40=x,` | sent as `0,` around gate changes; meaning unknown |
| `:w50=aaaaa,bbbbb,` / `:w51=…` | gate parameters of the current step for output 1 / 2 (5 digits each) |
| `:w60=1112111,II,VVVVV,` | calibration value II = VVVVV (sent by the calibration menu, link A) |
| `:w61=1114111,` / `:w61=1113111,` | store the calibration, with / without the backup copy (link A) |
| `:r11=,` | read measurement 1, reply `:r11=<value>.` *(Spooky2 biofeedback: current in mA, inferred)* |
| `:r12=,` | read measurement 2, reply `:r12=<value>.` *(biofeedback: phase angle, inferred)* |
| `:r80=0,` | read the calibration table; reply `:w80=13583792,c0,…,c20,` |

A preset with sweeps (Spooky2, captured: "Spooky Radionics (Boost) Manifestation", `Sine_WCM=11`):
13 `:a` table uploads, `:w20=11,` `:w21=25,` `:w32=20,` `:w33=220,` `:w11=1,,` `:w11=,1,` `:w14=1,`
`:w28=1000,` `:w29=1000,` (20 V at a 100 % offset is sent as 10.00 V), then only `:w24`: one line
for a fixed frequency, one about every 134 ms for a sweep (linear in time), each the frequency / 11.
The nx firmware plays this preset standalone with the same lines (`docs/nx/features.md` §9.17);
`tools/pc_replay.py` replays such a capture on the emulator.

A biofeedback scan (Spooky2, captured): set `:w24`, wait ~70 ms ("Delay for GX Inputs"), read
`:r11` and `:r12`, next frequency +0.025 %. See [`../BACKLOG.md`](../BACKLOG.md) for timings.

### Session / access gate

Two independent gates decide whether a command is answered:

- The **display** answers `:n00=$` always, but returns `:err` to the other `:n`/`:g`/`:p` frames
  unless its gate is open (input PB1 low, i.e. no PC, **or** an opened session). OpenGX Pro has no
  such gate and always answers them.
- The **generator board** returns `:err` to `:w`/`:r` commands unless a session is open, and `:ok`/data
  once it is. This gate is in the generator firmware and is unchanged by our display firmware.

The session is opened by a vendor handshake at the start of a Spooky2 connection (`:r90=…` challenge,
`:w92=…`, `:w91=…`); its algorithm is the vendor's device-authentication mechanism and is **not
documented here**. The one place this project sends it is the nx firmware's biofeedback scan
(`src/nx/nx_bfb.c`, a build switch that is off by default), to open the session for its own `:r11`
/ `:r12` reads, with the token rule of the Hunt & Kill port of Spooky2 (`docs/nx/features.md`
§9.18). Practical effect: without a running Spooky2 session, sending
these commands directly returns `:err` (well-formed frames) or nothing (malformed) — this is normal,
not a fault. To exercise the full command set on a real unit, use a tool that performs the
handshake (the `conformance` suite of the `spooky2` package in github.com/Polobase/freqgen does
this; `make conformance` runs it from a checkout of that repository next to this one).

## 5. What the display sends by itself

Each command waits for the generator's `:ok` (about 30–40 ms at most) before the next one.

| When | Link | Sequence |
|---|---|---|
| Power-up (HOME), program loaded, paused, stopped or finished | A and/or B | **idle configuration, outputs off:** `:w24=f,` `:w11=0,0,` `:w14=1,` `:w17=0,1,` `:w20=h0,` `:w21=h0,` `:w28=h1,` `:w29=h1,` `:w32=h2,` `:w33=h2,` |
| Program running, new step | A and/or B | `:w24=f,` `:w11=1,1,` then `:w12=0,0,` (no gating) or `:w50=a,b,` `:w51=a,b,` `:w12=1,1,` |
| nx: a built-in preset with sweeps is started | A or B | after the idle configuration: `:a11=…` and `:a25=…` (its two wave tables, each answered before the next line), `:w17=0,0,` `:w21=25,` `:w33=220,`, then the step; in a sweep step a `:w24=f,` every 125 ms |
| Program ends without loop | A and/or B | `:w11=0,0,` |
| PC mode starts (USB connected) | A, then B | `:w11=0,0,` (PB2 pulsed high around it) |
| PC mode ends | A, then B | `:w15=0,0,` |
| Calibration menu (G1+G2 held at power-up) | A, A+B | `:r80=0,`, test signals per menu position (`w11`, `w13`, `w24`/`w25`, `w28`/`w29`, `w32`/`w33`), `:w60=…` when a value changes, `:w61=…` on save |

At power-up both generators always get the idle configuration of an empty program
(`f = 0`, `h0 = 0`, `h1 = 0`, `h2 = 120`).

## 6. Other serial interfaces

### Bootloader (programming mode: power off, hold Stop, press Power)

Handled by the vendor bootloader; the flasher finds it on link A (`…903`): `AR` / `AN` return the hardware ID
(`X`, `2`), `AD` starts the update (reply `D`), then 2060-byte packets `AB…` with CRC-16, each
acknowledged with `C`. Use `tools/flash_gx_pro.py`.

### Factory connector (USART3, 5-pin `GND 5V Rx Tx FSEL`, not on USB)

`:a0` calibration screen / `:a1` back to normal, `:bN` select calibration point N and send its test
signal, `:cN` set its value, `:d11141` / `:d11131` store with / without backup, `:e`…`:l` set values
21–28. Every command is answered with `Z`.

### OpenGX Pro link probe (`make APP=probe`)

Test firmware only; lines start with `#` so the generator boards ignore them: `#id`, `#ping`
(answered on all three UARTs), `#pb2 0|1`, `#dump AAAAAA LLLLLL` (hex dump of the SPI flash with
CRC-32). Use `tools/gx_link.py`.

## 7. Examples

Version and a name (link A):
```
→ :n00=$
← :Rev201
→ :n01=?
← :n01=Schumann Resonance (CAFL)
```

Upload a 3-step program to slot 09 (1 s per step, 20 V, no gating) and name it:
```
→ :g09=0,0,0,0,0,0,
← :ok
→ :p09=0,2000,120,1,3,100007,200007,300007,
← :ok
→ :n09=Test 1-2-3 kHz
← :ok
```
(`100007` = 1 000 Hz, `200007` = 2 000 Hz, `300007` = 3 000 Hz.)

Read it back:
```
→ :n09=*
← :p09=0,2000,120,1,3,100007,200007,300007,
```
