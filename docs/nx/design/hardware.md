<!-- design note of 2026-09-23: Hardware capabilities and limits of the display board.
Superseded where docs/nx/fw2-design.md decides differently. -->

# GX Pro display board: hardware capabilities and limits

**Confidence tags:** **[FW]** means verified from firmware code, from the disassembly of the original image, or measured on the unit. **[PCB]** means seen on the board, the silkscreen or the rear photo. **[DS]** means from the STM32G070 datasheet DS12766. **[I]** means inferred.

**Citation keys.** Paths are relative to the repository root:

| Key | Path |
|---|---|
| hw | `docs/spec/hardware.md` |
| disp | `docs/spec/display.md` |
| proto | `docs/spec/protocols.md` |
| appspec | `docs/spec/application.md` |
| cmds | `docs/serial-commands.md` |
| hwreadme | `docs/hardware/README.md` |
| sch | `docs/hardware/schematic/build_schematic.py` |
| DS | `docs/hardware/DS_stm32g070cb.pdf` (the datasheet, not in git; page numbers) |
| photo | `docs/hardware/photos/display_board_rear.jpg` (kept locally, not in git) |
| sources | `board.c`, `board.h`, `lcd.c`, `lcd.h`, `keys.c`, `keys.h`, `spiflash.c`, `spiflash.h`, `uart.c`, `uart.h`, `startup.c`, `app.c`, `link.c`, `storage.c`, `state.h`, `main_bringup.c`, `main_probe.c` in `src/` and `src/classic/` |
| ld | `ld/app.ld` |
| map | `build/fw/gxpro.map` |
| backlog | `BACKLOG.md` |
| sim | `tools/sim/devices.py` |

The SVG sheets add no labels beyond `build_schematic.py`.

---

## 1. Display-board pin map (STM32G070RBT6, LQFP-64)

### 1.1 Pins the firmware uses

| Pin (LQFP64 #) | Function | Config | Tag | Citation |
|---|---|---|---|---|
| PF0 (10) / PF1 (11) | 8 MHz HSE crystal, HC-49/S with C2/C3 | reset state | [FW][PCB] | hw:157,230; photo |
| PA13 (45) / PA14 (46) | SWD, to the unpopulated 1×5 header next to R5 | reset state | [I][PCB] | hw:231; sch:356-357 |
| PA5/PA6/PA7 (22/23/24) | SPI1 SCK/MISO/MOSI to the SPI NOR flash | AF0, very high speed, mode 3, 32 MHz | [FW] | board.c:93-95; spiflash.c:43-46 |
| PA1 (18) | SPI flash CS# | output, high at init | [FW] | board.h:17-18; board.c:90 |
| PB13 (33) / PB15 (35) | SPI2 SCK / MOSI to the LCD (write-only) | AF0, very high speed | [FW] | board.c:113-114; lcd.c:108-116 |
| PA8 (36) | LCD CS# | output | [FW] | board.h:9-10 |
| PC6 (38) | LCD D/C | output | [FW] | board.h:11-12 |
| PC7 (39) | LCD RESET# | output | [FW] | board.h:13-14; lcd.c:127-130 |
| PB12 (32) | LCD backlight through T2 (SOT-23) with R7/R9/R10; TIM3 software PWM | output | [FW][PCB] | board.c:155-163; sch:412-417; photo |
| PA9 (37) / PA10 (42) | USART1 TX/RX. Reaches **generator A (G1)** and the PC's **second USB channel** (link A; …903 on the unit measured). The vendor bootloader also listens here | AF1, RX pull-up | [FW measured] | hw:474; cmds:15 |
| PA2 (19) / PA3 (20) | USART2 TX/RX. Reaches **generator B (G2)** and the PC's **first USB channel** (link B; …901 there) | AF1, RX pull-up | [FW measured] | hw:474; cmds:16 |
| PB8 (62) / PB9 (63) | USART3 TX/RX, factory/calibration port. Goes to the populated 5-pin JST `GND 5V Rx Tx FSEL`, which has no cable fitted | AF4 | [FW][PCB] | board.c:108-109; sch:373-378; photo |
| PB1 (28) | Input with pull-up. **High = USB/PC connected (PC mode)**, low = standalone | input | [FW measured] | hw:210,471; app.c:104-135 |
| PB2 (29) | Output. **1 = PC isolated, the display owns the generator links. 0 = PC ↔ generator/display links connected.** After a long POWER press it toggles forever as the ~37.6 kHz power-off signal | output, low at init | [FW measured] | hw:211,474-475; board.c:183-191 |
| PB3/PB4/PB5/PB11/PD0/PD1/PD2/PD3 | 8 keys (see §4) | inputs with pull-ups | [FW] | board.c:103-120; keys.c:5-31 |
| PB7 (61) | 16-step software PWM, duty 1/16 by default. It is changed only by factory `:b` (1/6/10/15 by calibration channel group). Load unknown | output | [FW] | hw:216; board.c:160; app.c:316 |
| PB0 (27) | Output held low with pull-down. Purpose unknown | output | [FW] | hw:209; board.c:100 |
| PB6 (60) / PB10 (30) | Inputs with pull-ups, **never read**. Possibly unpopulated key positions; the PCB has only 8 key footprints | input | [FW][PCB] | hw:215,219; sch:437-440 |

### 1.2 Pins the firmware never configures

These stay in the reset analog state and their routing is unknown [FW]: PA0, PA4, PA11, PA12, PA15, PB14 (SPI2_MISO, so the LCD is never read), PC0–PC5, PC8–PC15, PD4–PD6, PD8, PD9. Pin 6 (VBAT) and pin 7 (VREF+) are probably tied to 3V3 [I].

### 1.3 Connectors and spare-pin reuse

Alternate functions are from DS Tables 12–15, pp.36-39.

- **Factory 5-pin JST.** The housing is soldered on; only the cable is missing [photo]. It carries 5V, GND, PB8, PB9 and FSEL.
  - PB8 can be TIM16_CH1 (AF2) or I2C1_SCL (AF6).
  - PB9 can be TIM17_CH1 (AF2) or I2C1_SDA (AF6).
  - Both pins are 5 V tolerant (FT_f) [DS p.35].
  - This makes it the easiest expansion port with no soldering: an I2C sensor, an external battery-backed RTC module, a UART bridge, or a hardware-PWM piezo.
  - **FSEL hypothesis [I]:** FSEL is probably PB7. PB7's PWM duty encodes which output is being calibrated (factory `:b`), which a test jig could decode. This needs a continuity check. PB7 also offers TIM17_CH1N (AF2), so hardware PWM is possible there.
- **3-pin `U4Tx U4Rx GND` header (unpopulated).** USART4 can only be PA0/PA1 or PC10/PC11, and PA1 is the flash CS. So this header is most likely **PC10/PC11** [I]. It would give a free fourth UART. PC10/PC11 also offer TIM1_CH3/CH4 (AF2).
- **Other free pins with timer channels:** PB0 has TIM3_CH3 and ADC_IN8. PB6 has TIM1_CH3, TIM16_CH1N and I2C1_SCL. PB10 has ADC_IN11 and I2C2_SCL but no timer. All of these depend on unknown routing.
- **PB12 has no timer channel.** Its AFs are SPI2_NSS, TIM1_BKIN, TIM15_BKIN and EVENTOUT [DS p.37]. The backlight therefore **cannot use hardware PWM**.

### 1.4 Buzzer, LEDs, battery, 32 kHz crystal

- **None exists on the display board.** The rear photo shows only the MCU, SOIC-8 flash, HC-49/S crystal, SOT-223 LDO (J1), T2, a few R/C parts, connectors and 8 key footprints. There is no buzzer, no LED, no 32.768 kHz crystal and no battery [PCB, rear side only].
- **The firmware has no sound code.** Only PB7 and PB12 have software PWM [FW, hw:281].
- **Main-board LEDs are not reachable from the display MCU.** These are the rail LEDs and the FPGA "Done" LED, all driven by hardware [PCB, sch:252,260].
- **PB7** might drive an LED. If it does, something on the unit would glow at 1/16 duty during normal use, which can be checked by eye.

---

## 2. Peripherals and clocks

| Peripheral | Use today | Free capacity [DS] |
|---|---|---|
| Clock | HSE 8 MHz → PLL M=1, N=16 (VCO 128 MHz), R=/2 → **64 MHz** SYSCLK = HCLK = PCLK. Flash has 2 wait states, prefetch and I-cache. Falls back to HSI×8 if HSE fails (board.c:28-70). HSE start was confirmed on the unit (hw:467) | 64 MHz is the **maximum**, so there is no headroom |
| LSE / LSI / RTC | Not used (hw:166). **No 32.768 kHz crystal** [PCB] | RTC can run from **HSE/32 (crystal-accurate, only while powered)** or LSI (±5 %) [DS p.19,25]. 5 × 32-bit backup registers survive resets but not power loss without VBAT |
| TIM3 | 10 kHz tick (PSC 63, ARR 99), IRQ at priority 0. Drives both software PWMs, time base, hold counter and marquee (board.c:123-163; app.c:17-42) | — |
| TIM1, TIM6, TIM7, TIM14, TIM15, TIM16, TIM17 | Not used | All free. TIM14/16/17 have CC1 interrupts even without an output pin |
| SysTick | Not used. `reset_c` disables it and the handler is an alias of `Default_Handler` (startup.c:29,115) | Free. **Caution:** any IRQ without its own handler runs `power_off_signal()` (startup.c:15-23), so new DMA or timer IRQs must define handlers |
| DMA1 (7 channels) + DMAMUX | **Not used. LCD transfers are polled** (lcd.c:50-58; disp:19) | All 7 free. DMA can serve SPI1, SPI2, USART1–4, ADC and timers [DS p.19-20,27] |
| SPI1 | SPI flash, 32 MHz, mode 3, polled byte-by-byte | TX/RX FIFO, 4–16-bit frames, hardware CRC [DS p.27-28] |
| SPI2 | LCD, 32 MHz, 8-bit frames, polled through the FIFO | Same features. **32 MHz is the maximum** (DS Table 64 p.77) |
| USART1/2/3 | 115200 8N1, RX interrupt into 512 B rings, blocking TX (uart.c:5,19-37,95-102). FIFO disabled | USART1/2 have an 8-byte FIFO (proto:38), RX timeout and DMA. USART3 has no FIFO |
| USART4 | Not used | Free, probably on the U4 header [I] |
| ADC | Not used | 12-bit, 2.5 MSps, 16 external + 3 internal channels: temperature sensor (TS_CAL1 at 0x1FFF75A8, 30 °C single point, 2.5 mV/°C), VREFINT (0x1FFF75AA) and VBAT/3 [DS p.10,22-23,74] |
| IWDG / WWDG | Not used (hw:281) | IWDG runs from LSI, up to 32.8 s timeout [DS Table 61]. Once started it cannot be stopped, so the endless `power_off_signal` loop would have to feed it |
| CRC, I2C1/2, EXTI, MPU | Not used | All free |
| Low-power modes | None. The main loop is paced by busy loops (app.c:13,48-52) | Sleep, low-power run, Stop 0/1, Standby [DS p.16]. Run current at 64 MHz is about 6.9 mA [DS Table 24] |
| Not present on the G070 | — | DAC, USB, RNG, AES, comparators, LPTIM |

---

## 3. LCD

**Basic facts**

- **Controller:** ST7789-type with a 240×320 GRAM. MADCTL is 0 (native portrait). The UI is landscape 320×240, done in software by pre-rotated bitmaps: X = 319 − y_phys, Y = x_phys [FW] (disp:16-23,146-162; lcd.h:1-5).
- **Interface:** 4-wire SPI, **write-only** (MISO/PB14 not configured), mode 3, 8-bit frames, **32 MHz** (lcd.c:108-116).
- **Clock headroom:** 32 MHz is already the SPI maximum of the G070 (PCLK/2) [DS p.77]. The ST7789 could accept more, so the MCU is the limit.
- **Colour:** RGB565 (COLMOD 0x05), high byte first (lcd.c:10-28). This unit uses panel variant A (0xFF00 at 0x08003C00, with INVON) (hw:466).
- **Unused controller features** (disp:118-119): sleep-in, display-off, **hardware vertical scroll** (this pans horizontally in landscape), partial mode, idle 8-colour mode, MADCTL landscape, RGB444 (0x03) and RGB666 (0x06).
- **Tearing-effect (TE) pin:** no MCU pin reads one, and the 18-pin FPC pinout is unknown (hwreadme:65). **TE is not usable unless PB6/PB10 or another pin turns out to be wired to it.**

**Backlight**

- PB12 is driven by 16-step software PWM at 625 Hz (board.c:159-161).
- Normal duty is 8/16 = 50 % (app.c:319). `w64` sets 8 or 0 (link.c:606-611). **100 % (16/16) is never used.**
- Finer dimming is possible with a spare timer: set PB12 on the update IRQ and clear it on the CC1 IRQ, for any resolution (for example 256 levels at 1 kHz). Fades and auto-dim are also possible.

**Framebuffer in 36 KB RAM (about 25.6 KB free including stack; see §6)**

| Buffer | Size | Fits? |
|---|---|---|
| Full-screen RGB565 | 153,600 B | no |
| Full-screen 8 bpp | 76,800 B | no |
| Full-screen 4 bpp | 38,400 B | no |
| Full-screen 2 bpp (4-colour palette) | 19,200 B | yes, but takes almost all free RAM |
| Full-screen 1 bpp | 9,600 B | yes |
| Strip buffers (landscape row width 320 px, RGB565 = 640 B per row) | 8 rows = 5,120 B; two for DMA ping-pong = 10,240 B | yes, realistic |
| Widget buffer 120×80 RGB565 | 19,200 B | tight |

The panel keeps its own GRAM, so streaming directly without a framebuffer, as today, stays viable.

**Throughput (bus-bound: 32 MHz = 4 MB/s = 2.0 Mpx/s in RGB565)**

| Area | Time | Rate |
|---|---|---|
| Full screen, RGB565 | 38.4 ms | ≤ 26 fps |
| Full screen, RGB444 | 28.8 ms | ≤ 34 fps |
| Full screen, RGB666 | 57.6 ms | ≤ 17 fps |
| 320×24 text row | 3.8 ms | |
| Frequency readout (~24×200) | 2.4 ms | |
| 320×40 header | 6.4 ms | |
| 100×100 widget | 5 ms | |

- The window setup (CASET/RASET/RAMWR, 11 bytes) costs about 3–5 µs. Per-pixel windows, as in the original's rounded corners, are therefore expensive.
- Today's FIFO streaming is already close to the bus limit [I], and the CPU is 100 % busy while drawing. **DMA with 16-bit frames would mainly free the CPU**, for example for zero-CPU fills. It does not raise the bus ceiling.

---

## 4. Keys

- **8 physical keys** [PCB, 8 footprints on the photo]. Each is on its own GPIO, active low with an internal pull-up [FW].

| Code | Pin | Behaviour-derived name |
|---|---|---|
| 1 | PD0 | LOOP |
| 2 | PD1 | START/OK |
| 3 | PB5 | STOP |
| 4 | PB4 | UP |
| 5 | PD3 | DOWN |
| 6 | PB3 | G1 |
| 7 | PD2 | G2 |
| 9 | PB11 | POWER |

- **Combos the original decodes:** G1+G2 (code 8) at boot enters calibration. POWER+STOP (code 10) is unused in the app (keys.h:6-18; appspec:108-122).
- **Label confidence:** the physical labels are unconfirmed (appspec:593). STOP and POWER are confirmed by the vendor's "hold Stop, press Power" bootloader combo (README.md:80-84).
- **Any combination is detectable.** Since every key is its own GPIO, any multi-key chord, long press or double press can be read. `keys_read()` only returns the highest-priority code (keys.c:5-31).
- **Debounce and hold timing:**
  - The original does a 13.3 ms busy delay and needs two equal scans (app.c:13,48-69; appspec:124-145).
  - Hold is counted in 25.6 ms units.
  - Auto-repeat starts after more than 0.28 s. Power-off triggers when POWER is held more than about 0.8 s (app.c:351).
- **EXTI limit:** EXTI lines are shared by pin number, so PB3 and PD3 (G1/DOWN) collide, and so do PB1 and PD1 (PC sense/START). Not all keys can have their own interrupt; polling is fine.
- **POWER while off** is handled in hardware (there is no power-on code) [I].

---

## 5. SPI NOR flash

**Part:** JEDEC ID **EF 40 15 = Winbond W25Q16**, 2 MB, 256 B pages, 4 KB sectors [FW measured] (hw:469; spiflash.h:10-12).

**Timing:**

- Sector erase: 45 ms typical, 400 ms maximum.
- Page program: 0.4 ms typical, 3 ms maximum.
- Block erase: 32 KB in 120 ms, 64 KB in 150 ms. Chip erase: 5 s typical.
- Sources: sim:136; spiflash.c:61-62.
- Endurance is not in the repo. The W25Q16 datasheet gives about 100k cycles per sector and 20 years retention [I].
- The link is single-SPI at 32 MHz, so reads peak at 4 MB/s.

**Current layout** (storage.c:5-9; hw:385-394):

| Address | Content |
|---|---|
| 0x05000 | Names, 32 × 60 B |
| 0x11000–0x2E000 | Program slots 1..30 at 0x10000 + n×0x1000 (0x668 B record + 800 B step table each) |
| 0x74000 | Calibration table (64 B) |
| 0x75000 | Calibration backup |

- A real SPI-flash backup confirms nothing else is used (appspec:610). Fonts and images are **not** stored there; they sit in MCU flash as 11.4 KB of vendor assets (map, assets.o).

**Free space:**

- 33 sectors (132 KB) are used. The original may also erase 0x10000 and 0x2F000 (storage.c:114-117). **About 477 of 512 sectors (≈1.86 MB) are free.**
- **Safest region: 0x080000–0x1FFFFF (1.5 MB).** The vendor firmware never touches it, so it survives a return to the original firmware.
- Slots 31..99 would also fit exactly below the calibration sector (0x10000 + 99×0x1000 = 0x73000) [I].

**What 1.5 MB holds, for example:**

- About 10 full-screen RGB565 images (150 KB each).
- About 160 full-screen 1-bpp images.
- Many anti-aliased fonts (a 24 px, 4-bpp ASCII font is about 14 KB).
- About 49k log records of 32 B.

**Unused chip features [I]:**

- Erase suspend/resume (0x75/0x7A), so reads can continue during a 45–400 ms erase.
- Fast read (0x0B) and 32K/64K block erase.
- 64-bit unique ID (0x4B).
- Security registers. **Do not lock them.**

**Power-loss risk:** the current writes are erase-then-write with no journal. New data should use CRC-checked, double-buffered records.

---

## 6. MCU flash and RAM budget

**Flash** (map:742-743, 783-788, 1294-1303; ld:7-8):

- The app region is 112 KB (114,688 B).
- The image uses **31,444 B (27.4 %)**:
  - `.isr_vector`: 184 B
  - `.text` + `.rodata`: 31,180 B (0x79CC)
  - `.ARM.exidx`: 8 B
  - `.data` load image: 72 B
- **Free: 83,244 B (≈81.3 KB).**
- Largest contributors:

| Module | Bytes |
|---|---|
| assets.o (vendor fonts/bitmaps, extracted at build time) | 11,461 |
| link.o | 3,397 |
| ui.o | 3,340 |
| calib.o | 2,228 |
| app.o | 2,108 |
| gfx.o | 1,982 |
| board.o | 1,140 |
| libgcc | ≈1.5 KB |

- Internal flash has 2 KB pages, 22–40 ms erase and **only 1k endurance cycles** [DS p.59]. It is fine for code and rarely updated assets, not for settings.
- The original image was 74,672 B, so the bootloader accepts at least about 73 KB. Full 112 KB images are untested [I]. (Tested on 2026-09-25: images up to 112,640 B are proven, `docs/nx/drop0-checklist.md` §1.)

**RAM** (map:780, 1303-1414):

- `.data` is 72 B and `.bss` is 11,196 B, so **11,268 B are used (30.6 % of 36,864)**.
- **25,596 B remain** for stack and new buffers. The stack grows down from 0x20009000, and the linker only asserts at least 2 KB (ld:12,55).
- The stack already runs in the top 4 KB on the real unit, which confirms 36 KB mode (parity off).
- Largest RAM users:

| Symbol | Bytes |
|---|---|
| `g_ch` | 4,960 |
| `g_name_bmp` | 1,998 |
| `g_names` | 1,920 |
| UART rings | 1,548 |
| link rx | 688 |

- **Realistic budget for new buffers: about 20 KB**, keeping 4 KB or more of stack.

---

## 7. What the display can control on the main board and generators

**Architecture.** The main board has an MCU (LQFP-48), an FPGA-like BGA (U201), two resistor-ladder DAC banks and two op-amp chains for generator A (3xx) and generator B (4xx). An amp board carries 4 power op-amps on ±12 V (outputs A1, A2, B1, B2). There is also a GxExt daughterboard with two mirrored analog sections of unknown function [PCB, hwreadme:40-55; sch:249-314]. **The display generates no waveforms itself** (the G070 has no DAC). It only sends ASCII commands at 115200 baud over USART1 (A) and USART2 (B).

**Commands the display can send** (cmds:97-117; proto:260-294):

| Command | Meaning |
|---|---|
| `w11` | Output 1/2 on or off |
| `w12` + `w50`/`w51` | Per-step gating |
| `w14` | Output 2 follows output 1 |
| `w20`/`w21` | Waveform. Stored programs use 41–48; Spooky2 uses 11 and 25; the code meanings are unknown |
| `w24`/`w25` | Frequency, encoded as `m·10^(e−8)` Hz, resolution 10⁻⁸ Hz. The display shows up to 8 integer digits, so < 100 MHz (proto:300-304) |
| `w28`/`w29` | Amplitude in 0.01 V. User programs use 2000 = 20.00 V. With ±12 V amp rails this is plausibly Vpp [I] |
| `w32`/`w33` | "Offset", default 120 |
| `w13`, `w15`, `w17`, `w36`, `w37`, `w40` | Meaning unknown |
| `w60`/`w61`/`r80` | Generator calibration. **Hazardous** |
| `r02` | Generator firmware version (reply 200) |
| `r11`/`r12` | Biofeedback readings, probably current and phase. Spooky2 captures show r11 ≈ 46–49k and r12 ≈ 5.8–7.5k (backlog:28) |

**Documented output range:**

- User data spans 7.83 Hz to 24–37 MHz (appspec:611).
- The calibration test runs up to 10.24 MHz and 21.54 V (appspec:506-509).
- The true hardware maximums are **not documented** in the repo.

**Throughput:** about 2–4 ms per generator command plus `:ok` (cmds:94-95; backlog:26), so roughly 250–500 commands/s per link. The two links run in parallel. `link_wait_ok` blocks for up to 40 ms per command (link.c:438-450).

**Session gate:**

- Generators answer PC-originated `:w`/`:r` only inside a vendor Spooky2 session (cmds:122-137).
- Display-originated `:w` commands work in standalone mode, because programs run on the unit.
- **Display-originated `r11`/`r12`/`r02` in standalone mode are untested** (backlog:44-45). This decides whether on-device biofeedback features are possible.
- The generator ignores lines that do not start with `:`, so `#…` lines are a safe extension namespace (hw:476).

---

## 8. Power

- **Power-off sequence** [FW, verified on the device]:
  1. POWER held more than about 0.8 s.
  2. Backlight 0, goodbye screen, backlight 8.
  3. Wait about 4 s.
  4. PB2 square wave forever, half-period `delay_loop(213)` ≈ 13.3 µs, so about 37.6 kHz (app.c:289-297; board.c:183-191).
  5. The main board presumably detects the AC signal, as distinct from PB2's DC bus-owner level, and cuts power [I].
- **Power-on** is hardware (there is no code for it) [I]. **The MCU is unpowered when off.** There is no battery or supercap and no LSE [PCB], so there is no timekeeping or scheduled wake across power-off.
- **Supply:** 5 V comes over the cable to the J1 3.3 V LDO (SOT-223). The 2-pin cable next to J1 is probably the supply [I, photo].
- **PB1** reads 1 with USB connected (hw:471) and is low in standalone use. It is therefore actively driven by the main board. Whether it follows VBUS or the host opening the port is unknown.
  - On PB1 high, the original sets PB2=1, sends `:w11=0,0,` to both generators, then sets PB2=0 (PC mode).
  - On PB1 low, it sets PB2=1, sends `:w15=0,0,` and goes to HOME (app.c:104-135).
- **PB2 = 1 isolates the PC completely** (hw:475). In standalone mode the display cannot talk to the PC. In PC mode it shares the wire with the generator.

---

## 9. Summary

### Hard limits

1. **No full-colour framebuffer.** 4 bpp or more does not fit in 36 KB. Only 1 bpp (9.6 KB), 2 bpp (19.2 KB) or strip buffers are possible.
2. **LCD bus is capped at 32 MHz single-lane SPI.** A full-screen redraw takes at least 38.4 ms (≤ 26 fps; 34 fps in RGB444). TE is unknown and unwired as far as known, so full-screen animation may tear.
3. **The CPU is already at its 64 MHz maximum.** Cortex-M0+ with no FPU and no hardware divider, so avoid float and 64-bit division in hot paths.
4. **No sound hardware.** No buzzer, DAC or speaker. Only add-on hardware on the factory JST or U4 header could provide it.
5. **No user LEDs** except the backlight (PB7's load is unknown).
6. **No wall-clock time.** No LSE, no VBAT battery, and the MCU loses power when off.
7. **No touch.** 8 keys only.
8. **Backlight PB12 has no hardware PWM.**
9. **Generator side is closed and fixed at 115200.** Everything is limited to the generator's ASCII command set; waveforms are only selectable by unknown codes; readbacks are only r02/r11/r12/r80; the Spooky2 session gate applies to the PC.
10. **The PC and the display share USART1/2 through the PB2 mux.** No PC link in standalone mode, and in PC mode the UI must stay passive.
11. **MCU-side restrictions.** Never touch the bootloader (0x08000000–0x08003FFF) or option bytes. Under RDP1 an SWD connection blocks flash, so on-target debugging is not practical. Internal flash has only 1k write cycles.
12. **Protocol-fixed formats for backward compatibility.** 30 slots × 200 steps, 60-character ASCII names, 1.01 s dwell unit, `:n#` dumps only 200 values. The vendor font is ASCII only, so new fonts must be your own.

### Underused capabilities

1. **DMA:** all 7 channels are free, for LCD (16-bit frames), flash reads and UART RX. This frees the CPU during redraws.
2. **ST7789 features:** hardware scroll (landscape pan and slide transitions), sleep and display-off, partial and idle modes, RGB444 for about 25 % faster transfers, MADCTL landscape.
3. **Backlight:** 100 % brightness is never used. Timer-IRQ PWM with fine steps, fades, auto-dim and screen-off.
4. **Timers:** TIM1/6/7/14/15/16/17 and SysTick are free, for precise dwell timing (the current tick runs 1 % long), timer-based UI pacing instead of 13.3 ms busy loops, and Sleep (WFI).
5. **IWDG/WWDG, RTC and backup registers:** a watchdog, reset-reason and crash counters, and a session and uptime clock from HSE/32.
6. **ADC:** internal temperature sensor, VREFINT (VDD monitor), and free ADC pins if routed.
7. **CRC unit:** integrity checks for new SPI-flash records.
8. **About 1.5 MB of SPI flash untouched by the vendor**, plus W25Q16 erase-suspend and unique ID. Uses include fonts, images, favourites, history, logs, and slots 31–99.
9. **About 81 KB of MCU flash and about 20 KB of RAM free.**
10. **Expansion paths:** the populated factory JST (5V, GND, PB8/PB9 with I2C1, TIM16/17 PWM or USART3), the U4 header (USART4, probably PC10/PC11), and the unused inputs PB6/PB10.
11. **Keys:** full multi-key chords, long press and double press.
12. **`#`-prefixed extension commands in PC mode**, which generators ignore.

### Check on hardware before relying on

- PB7 → FSEL, and PB0, PB6, PB10: continuity checks.
- FPC pinout: is TE wired, is LCD SDO wired?
- Is VBAT tied to 3V3?
- Does PB1 follow VBUS or port-open?
- Do generators answer display-sent `r11`/`r12`/`r02` in standalone mode?
- Maximum image size the bootloader accepts (done on 2026-09-25: 112,640 B proven, `docs/nx/drop0-checklist.md` §1).
- Waveform code table (`w20`/`w21`).
- The unphotographed front side of the display board.
