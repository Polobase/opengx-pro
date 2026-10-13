# Serial protocols of the GX Pro display MCU

Scope: the UART links, the command set the display **accepts** on them, the messages it **sends**
to the generator boards, and the number helpers behind them. Screens, keys and hardware details are
covered in the other specs and are only mentioned here where the protocol depends on them.

Status labels: **VERIFIED** means read directly from the disassembly or decompilation at the cited
address. **INFERRED** means deduced from how the display uses a value; the generator side is not in
this image. All addresses are in the application image (`0x08004000`-based).

Shorthand used below:

| Name | Address | Meaning |
|---|---|---|
| `ui_flags` | `0x2000000E` (u16) | main flag word (name from `application.tsv`) |
| `pc_flags` | `0x20000010` (u16) | "field is drawn" bits; the protocol *clears* a bit to request a redraw |
| `misc_flags` | `0x20000012` (u16) | bits 1..4 = output ON status of channels A1, A2, B1, B2 |
| `ui_state` | `0x2000001C` (u8) | UI state: 0x10 = PC/remote mode, 0xFF = calibration mode, 0..4 = standalone UI |
| "board A" / "board B" | USART1 / USART2 | the two generator ports (channels 1–2 and 3–4 on the screen) |
| `gate_open` | — | `(ui_flags & 0x4000) != 0 \|\| (GPIOB_IDR & 2) == 0` (see §2.4) |

---

## 1. UART configuration (VERIFIED)

All three ports are set up with STM32 **LL** drivers (not HAL): `LL_USART_Init` at `0x0800ABF4`,
called with the struct `{Prescaler=DIV1, BaudRate=115200, DataWidth=8, StopBits=1, Parity=none,
Direction=TX_RX (0xC), HWFlow=none, OverSampling=16}`. Afterwards each driver calls `ConfigAsyncMode`
(`0x0800ABB4`) and `Enable` (`0x0800ABD4`), then busy-waits for TEACK and REACK
(`0x0800ACE2`, `0x0800ACC8`).

| | USART1 | USART2 | USART3 |
|---|---|---|---|
| Init function | `0x0800B218` | `0x0800B304` | `0x0800B3D4` |
| Settings | 115200 8N1, oversampling ×16 | same | same |
| TX pin | PA9, AF1, high speed, push-pull | PA2, AF1, high speed | PB8, AF4, low speed |
| RX pin | PA10, AF1, pull-up | PA3, AF1, pull-up | PB9, AF4, pull-up |
| FIFO | disabled (`0x0800ABC8`), thresholds set to 0 (`0x0800AD88`, `0x0800AD78`) | same | not touched (the G070 USART3 has no FIFO) |
| NVIC | IRQ 27, priority 0 (`0x0800DAA0`/`0x0800DA10`) | IRQ 28, priority 0 | IRQ 29 (USART3_4), priority 0 |
| Interrupts | RXNEIE + PEIE, set in `main` at `0x0800E458…0x0800E476` (`0x0800ABEA`, `0x0800ABDE`) | same | same |
| RX handler | `USART1_IRQHandler` `0x0800C758` | `USART2_IRQHandler` `0x0800CF90` | `USART3_4_IRQHandler` `0x0800D7D8` |
| Frame processing | `uart1_frame_handler` `0x0800F22C` (main loop) | `uart2_frame_handler` `0x0800F9C4` (main loop) | `factory_cmd_execute` `0x0800522C` (main loop) |
| TX byte | `uart1_putc` `0x08010B88` | `uart2_putc` `0x08010BA8` | `uart3_putc` `0x08010BC8` |
| Role (INFERRED) | Port to generator board A. It also carries host (PC) frames that reach the display on the same line | Port to generator board B, same arrangement | Factory calibration / test-jig port |

**Inferred topology.** USART1 and USART2 each carry two kinds of traffic:
- the display's own commands to a generator board, which the board answers with `:ok`;
- host frames (`:g`, `:p`, `:n`, `:w…`), which the display answers with `:ok`, `:err` or data.

The display also snoops `:w11/14/15/24/25` frames on these lines to mirror channel state on the
screen. The image does not show how the lines are physically wired. The most consistent reading is
that each generator MCU sits between the PC's USB serial and the display: it forwards host frames
and returns the display's answers. PB1 is an input that selects PC/remote mode (§5.2). PB2 is an
output that toggles around host-mode changes; its purpose is INFERRED as a bus-owner or
generator-control line (see the hardware spec).

## 2. Low-level mechanics

### 2.1 Transmit (VERIFIED)
- Transmit is blocking and one byte at a time: `LL_USART_TransmitData8` (`0x0800AD96`) writes TDR,
  then the code polls ISR.TC (bit 6) until it is set (`0x08010B88/BA8/BC8`). No TX interrupts and no
  DMA.
- Each board port has a 64-byte TX staging buffer: `uart1_txbuf` `0x200000F0` and
  `uart2_txbuf` `0x20000130`. `uart1_send_txbuf(n)` `0x0800BF88` and `uart2_send_txbuf(n)`
  `0x0800C2A8` send the first *n* bytes.
- All replies and commands end in `\r\n` (0x0D 0x0A).
- USART3 only ever sends the single byte `'Z'` (0x5A), with no CR/LF.

### 2.2 USART1/USART2 receive parser (VERIFIED, `0x0800C758` / `0x0800CF90`)
The two ports use identical code with separate state. In the table below the USART2 names come
second.

| Item | USART1 | USART2 |
|---|---|---|
| state `s` (u8) | `0x20000068` | `0x20000069` |
| rx buffer `b[0x44]` | `0x20000170` | `0x200001B4` |
| frame id (u16: `letter<<8 \| number`) | `0x2000006A` | `0x2000006C` |
| value index `idx` (u16) | `0x20000064` | `0x20000066` |
| frame-pending bit in `ui_flags` | bit 10 (0x400) | bit 11 (0x800) |
| `:w` argument array (u64[32]) | `0x20002458` | `0x20002558` |
| `:p` array (u64[205]) | `g1_program` `0x20000978` | `g2_program` `0x20000FE0` |
| `:g` array (u16[400]) | `g1_step_params` `0x20001648` | `g2_step_params` `0x20001968` |

Per received byte `c` (the handler checks only RXNE, then reads RDR):
1. If the port's frame-pending bit is set, the byte is **discarded**. This includes `:ok` replies.
2. `s==0 && c==':'` → `s=1`.
3. `s==1 && c ∈ {w,p,g,n}` → `b[1]=c`, `s=2`.
4. `s==1 && c=='o'` → `gen_ok_state (0x2000000B) = 1`, `s=2`.
5. `s==2 && c=='k'` → `gen_ok_state = 2`, `s=0`. This fires for *any* `k` in state 2, not only after `o`.
6. `s==2 or 3` → `b[s] = c & 0x0F`, `s++`. The number must be exactly two characters and is not
   checked to be digits.
7. `s==4 && c=='='` → `s=5`, `id = b[1]*256 + b[2]*10 + b[3]`.
8. Any other byte while `s<5` → `s=0`.
9. `s≥5`, handled per letter:
   - `g`, `p`, `w`: the separator `','` **or** `'.'` ends a value. The value is parsed from
     `b[5..s-1]`, using `(char & 0x0F)` as the digit with no validation. If the token is empty, the
     value is all-ones (`0xFFFF` or `0xFFFF…FFFF`). The value is stored at `array[idx]`, then
     `idx++` unless `idx` equals the letter's last index (`g`: 399, `p`: 204, `w`: 31; an
     equality test, so an `idx` already past it keeps counting and writes behind the array).
     Then `s=5`. `'\n'` ends the frame:
     `s=0`, pending bit set. A token of 27 or more characters (`s==0x1F`) aborts the frame with
     `s=0`. Other bytes are appended to `b`. `g` values are u16 and wrap; `p`/`w` values are u64
     (`__aeabi_lmul`).
   - `n`: `'\r'` stores a 0 byte. `'\n'` zero-fills `b[s..0x43]`, sets `s=0` and sets the pending
     bit. If `s==0x40` on a non-CR byte, the frame is aborted and `b[0x40]=0`. Other bytes are
     appended raw.
10. After every byte that was not returned from early, the handler checks whether the frame is
    `'w'` and pending and its number is not 63. If so, it runs the immediate `w` dispatch (§3.1).

Notes (VERIFIED):
- The trailing token before `\r\n` is **not** converted. Every value must end with `,`. Example:
  `:w24=3750007,\r\n`.
- `idx` is reset only after a `w`, `p` or `g` frame is handled, not at the start of a frame and not
  by an `n` frame (its exits `0x0800F960` / `0x08010122` only clear the pending bit). If a frame is
  aborted, the next frame's values start at a non-zero index.

### 2.3 Generator handshake: `gen_wait_ok` `0x08012524` (VERIFIED)
The function clears `gen_ok_state`, then polls up to 200 000 times for `gen_ok_state == 2`, then
always waits another `delay_loop(0x855)`. A timeout is not reported and nothing is retried. The
next command is sent either way.
- USART1 and USART2 **share** `gen_ok_state`, so an `:ok` on either port satisfies the wait.
- `:err` from a board is not recognised and simply ends in the timeout.
- Timing (INFERRED, assumes a 64 MHz core): each call to `delay_loop(n)` (`0x08012518`) takes about
  n/21 333 ms. That gives 0x855 ≈ 0.1 ms, 0x34155 ≈ 10 ms, 640 000 ≈ 30 ms. The `gen_wait_ok`
  timeout is on the order of 30–40 ms.

### 2.4 Access gate (VERIFIED)
Several host commands are accepted only when `gate_open`: PB1 is low (standalone), **or**
`ui_flags` bit 14 (0x4000) is set. Bit 14 is set and cleared only by excluded mechanisms (§10).
A reimplementation should keep it as an opaque "host access" flag.

### 2.5 USART3 receive parser (VERIFIED, `0x0800D7D8`)
Frame format: `:` + one letter `a`…`l` + decimal digits + `\n`.

| Item | Address |
|---|---|
| state (u8) | `0x200000BE` |
| command letter | `0x20000070` |
| number (u16, wraps) | `0x20000072` |
| frame-pending bit | `0x20000014` bit 0 |

- While a frame is pending, incoming bytes reset the state and are dropped.
- Non-digit, non-LF bytes after the letter are ignored while `2 ≤ state ≤ 9`; beyond that the
  parser resets. Digits increase the state without a bound.
- The main loop calls `factory_cmd_execute` (called at `0x0800F1E4`), which clears the pending bit.
- At boot, `main` shows the splash screen and waits up to 100 × `delay_loop(640000)` (about 3 s)
  for a USART3 frame (`0x0800E516`). A frame ends the wait early. This is skipped when key 8 is
  held at power-on.

---

## 3. Incoming command set, USART1 / USART2 (host → display)

The frame format is `:<letter><NN>=<v1>,<v2>,…,\r\n`, where `NN` is two decimal digits. For
`g`/`p`/`n` frames `NN` is the **program slot**; for `w` frames it is the command number. The
display **never answers `w` frames**.

### 3.1 `:w` commands handled directly in the RX interrupt (mirror of channel state)
These run only if `gate_open`. When the gate is closed, the frame stays pending and the frame
handler later drops it with no effect. After handling, the pending bit is cleared and `idx=0`.
Board B uses the bits given in brackets.

| Cmd | Args | Effect on RAM | Handler U1 / U2 |
|---|---|---|---|
| `w11` | `a1,a2,` (0/1) | `a1==1` → `misc_flags \|= 0x02 [0x08]`; `a1==0` → clear; `pc_flags &= ~1 [~4]`. The same for `a2` with `0x04 [0x10]` and redraw bit 1 [3]. Other values change nothing. | `0x0800CBF0` / `0x0800D428` |
| `w14` | `a1,` (0/1) | channel-sync flag `ui_flags` bit 12 [13] = `a1` (only 0/1 accepted); `pc_flags &= ~3 [~0xC]` | `0x0800CEAE` / `0x0800D6EE` |
| `w15` | `a1,a2,` (0/1) | `pc_flags` bit 6 [8] = `a1`. Bit 7 [9] = `a2`; if sync is on, it is instead set equal to bit 6. `pc_flags &= ~3 [~0xC]`. Bits 6–9 are not read anywhere else in the image. | `0x0800CCDC` / `0x0800D516` |
| `w24` | `f,` | `pc_g1_out1_freq (0x20000030) [pc_g2_out1_freq 0x20000040] = f`, redraw bit 0 [2]; if sync, also `pc_g1_out2_freq (0x20000038) [pc_g2_out2_freq 0x20000048] = f` and redraw bit 1 [3] | `0x0800CDE6` / `0x0800D624` |
| `w25` | `f,` | if sync: ignored (left pending, then dropped); otherwise `pc_g1_out2_freq [pc_g2_out2_freq] = f`, redraw bit 1 [3] | `0x0800CE52` / `0x0800D692` |

`f` uses the frequency encoding in §6.1. The mirrors are drawn only in PC mode (`ui_state==0x10`),
by `0x080065D8`. The colour of each channel shows its `misc_flags` bit.

The immediate dispatch **defers** these numbers to the frame handler: 16, 62 (U1) or 60 (U2), 63,
64, 80, 92, 96, 97, plus three further numbers that belong to excluded mechanisms (§10). Every
other `w` number is dropped in the interrupt.

### 3.2 `:w` commands handled by the frame handler (main loop)
All of these end with `idx=0` and the pending bit cleared (`0x0800F562`/`0x0800F986`,
`0x0800FCF6`/`0x08010148`). `aN` is `wargs[N]` of the receiving port.

| Cmd | Args | Condition | Effect | Handler U1 / U2 |
|---|---|---|---|---|
| `w16` | any | — | no effect | default `0x0800F560` / `0x0800FCF4` |
| `w62` | `3336333,` | U1 only, `gate_open` | `cal_restore_backup()` `0x08011288`: if SPI-flash `0x75000` is not blank, copy 32 u16 into `cal_table` and rewrite `0x74000` if it differs | `0x0800F288` / — (U2 drops it in the ISR) |
| `w60` | `1112111,idx,val,` | U2 only; **reads USART1's `wargs`** (VERIFIED `0x0800FA20`) | if `U1wargs[0]==1112111`: `cal_table[U1wargs[1]] = (u16)U1wargs[2]` (index not range-checked). **Always** sets `misc_flags = 0`. | — / `0x0800FA20` |
| `w63` | `111999,` / `111000,` | `gate_open` | `cal_table[0]` +1 (cap 32100) / −1 (floor 31900), then `cal_save(0)` | `0x0800F2B8` / `0x0800FA4A` |
| `w64` | `18888881,` / `10000001,` | — | `0x2000001B = 8` / `0`. This is the soft-PWM duty of PB12 in `TIM3_IRQHandler` `0x0800C604`; INFERRED to be the backlight on/off. | `0x0800F340` / `0x0800FAD0` |
| `w80` | `13583792,v0,…,v20,` | — | `cal_table[i] = (u16)a[i+1]` for i = 0..20. RAM only. This is the reply to the display's `:r80=0,` (§5). | `0x0800F372` / `0x0800FB04` |
| `w92` | — | — | `delay(10 ms)`, PB2 := 1, `delay(10 ms)`, then send `:w91=0,\r\n` on the **same** port (no ok-wait). Purpose unknown. | `0x0800F3A2` / `0x0800FB90` |
| `w96` | `12321,` | not done since boot (`ui_flags` bit 15 clear); **not gated** | Erase SPI-flash sectors of slots 1..30 (`0x11000…0x2E000`), zero all RAM names, erase name sector `0x5000`, set bit 15 (so this runs at most once per boot) | `0x0800F42C` / `0x0800FBBE` |
| `w97` | `12621,` | `gate_open` | Board A [B]: `pc_g1_out1_freq`/`pc_g1_out2_freq` [`pc_g2_out1_freq`/`pc_g2_out2_freq`] := 0; sync flag bit 12 [13] := 1; `pc_flags &= 0xFF3C [0xFCF3]`; zero name slots 0 and 31 | `0x0800F4B4` / `0x0800FC46` |

### 3.3 Program upload and query frames (`g`, `p`, `n`)
The slot `NN` must be 1..30 for `g`/`p`. `:err\r\n` is 6 bytes and `:ok\r\n` is 5 bytes. Program RAM
and program state for board A come from USART1 and for board B from USART2. The flash **slots are
shared** by both ports.

| Frame | Condition for `:err` | On success | Handler U1 / U2 |
|---|---|---|---|
| `:gNN=g0,g1,…,\r\n` | `!gate_open` or `NN==0` or `NN>30` | Values go into program-gate RAM (A or B) starting at `idx`. Nothing is written to flash; the next `:p` frame stores them. Sets `g1_step_sent (0x20000061)` [`g2_step_sent 0x20000063`] = 0xFF, which forces the program sequence to be resent (§5.3). Reply `:ok`. | `0x0800F5FA` / `0x0800FD8E` |
| `:pNN=p0,p1,…,\r\n` | same | `program_save(NN, A\|B)` `0x080113EC`: erase SPI sector `0x10000+NN*0x1000`, write the p array (0x668 bytes) at +0 and the g array (800 bytes) at +0x668. Sets sent-step = 0xFF. Reply `:ok` after the flash write finishes. | `0x0800F56C` / `0x0800FD00` |
| `:nNN=<text>\r\n` | (`!gate_open` and `text[0] != '$'`) or `NN>30` | Sub-command chosen by `text[0]`, see below. On **U2, `NN==0` is changed to slot 31** (`0x0800FECA`). | `0x0800F6FE` / `0x0800FE9A` |

`:n` sub-commands (character `b[5]`):

| `text[0]` | Reply / effect |
|---|---|
| `*` | Sends `:pNN=` (5 bytes, NN echoed as received), then `program_dump_g1`/`program_dump_g2` (`0x08010DB0` / `0x0801101C`). This reads the slot from SPI flash. If `p[4]` is 1..200 it sends `p[0] … p[4+p[4]]` as `"<decimal>,"`, each value as its own write. Then `\r\n`. |
| `#` | Sends `:gNN=`, then `steptable_dump_g1`/`steptable_dump_g2` (`0x08010BE8` / `0x08010CCC`). This reads 800 bytes from `slot+0x668`. If `g[0] != 0xFFFF` it sends **the first 200** u16 values as `"v,"`, then `\r\n`. |
| `$` | `:Rev201\r\n` (9 bytes). Allowed even when the gate is closed. |
| `,` | Clears `b[5..0x40]`, erases the program sector of the slot, clears `pc_flags` bit 4 [5], replies `:ok`. The name in RAM is **not** cleared. |
| `?` | `:nNN=<name>\r\n`. The name is taken from `name_table[slot]` (`0x200001F8 + slot*60`). Copying stops at the first byte outside 0x20..0x7F; at most 57 characters. |
| anything else | `name_set(slot, text)` `0x080124D8`: copies ≤ 60 bytes up to NUL and zero-pads. If the slot is 1..30, `names_save()` `0x08011380` rewrites SPI sector `0x5000` with slots 1..30; slots 0 and 31 are written as zeros. Clears redraw bit 4 [5] and replies `:ok`. |

The dump formats match the upload formats, so `:n05=*` returns a line that could be uploaded again
as-is.

---

## 4. USART3 factory / calibration commands (VERIFIED, `factory_cmd_execute` `0x0800522C`)
Dispatch goes through `__ARM_common_switch8` `0x0800D9C8` using the table at `0x0800523A`. Every
command ends with the reply `'Z'` and clears the pending bit. `n` is the u16 number.

| Cmd | Handler | Effect |
|---|---|---|
| `:a<n>` | `0x08005248` | `n==0`: `cal_index (0x2000007C) = 0`, `ui_state = 0xFF` (calibration mode), redraw, clear the screen, draw the calibration screen (`0x0800DED4`). `n!=0`: `ui_state = 0` (back to the normal UI), redraw, clear the screen. |
| `:b<n>` | `0x080052A6` | `cal_index = n`; `cal_apply_test_jig()` `0x08011C2C` (sends signals, §5.4); redraw; sets the PB7 soft-PWM duty `0x2000000C` to 1 for n≤5, 6 for 6–10, 10 for 11–15, 15 for 16–20, otherwise 1 |
| `:c<n>` | `0x08005324` | `cal_table[cal_index] = n`; sends `w60` for (cal_index, n) on U1 (§5.1); redraw |
| `:d<n>` | `0x08005352` | `n==11141` → `cal_save(1)` then send `:w61=1114111,`; `n==11131` → `cal_save(0)` then send `:w61=1113111,`. Both go to U1 with no ok-wait. |
| `:e<n>`…`:l<n>` | `0x0800538A`, `53AC`, `53CE`, `53EE`, `5418`, `5438`, `5458`, `5478` | `cal_table[21..28] = n` for e..l respectively, and the value is drawn with `0x08006384` at fixed positions |

`cal_save(mode)` `0x08011310` writes `cal_table` (32×u16) to SPI `0x74000` if it differs. When
`mode != 0` it also writes the backup copy at `0x75000`.

---

## 5. Outgoing messages (display → generator boards)

### 5.1 Message builders (VERIFIED)

| Function | Format sent |
|---|---|
| `uart1_send_line(s)` `0x0800C268` / `uart2_send_line(s)` `0x0800C4AC` | `s` + `\r\n`; `s` is a fixed literal such as `:w11=1,1,` |
| `uart1_send_w_u64(id, v)` `0x0800C084` / `uart2_…` `0x0800C2C8` | `:w<id as 2 digits>=<v in minimal decimal>,\r\n`. The first digit's divisor comes from `u64_pow10_floor(v)` `0x0800E200`. At most 16 division steps, so v ≥ 10^16 is misformatted. |
| `uart1_send_w_pair5(id, a, b)` `0x0800C140` / `uart2_…` `0x0800C384` | `:w<id>=<a as 5 digits, zero-padded>,<b as 5 digits>,\r\n` (19 bytes) |
| `uart1_send_cal_value(idx, v)` `0x0800BFA8` | `:w60=1112111,<idx as 2 digits>,<v as 5 digits>,\r\n` (24 bytes), U1 only |

### 5.2 Messages, triggers and waits

| Message | Port | Trigger (site) | Waits for `:ok`? |
|---|---|---|---|
| `:r80=0,` | U1 | Boot with key 8 held → calibration mode (`0x0800E4E2`). The expected reply is `:w80=13583792,<21 values>,`, handled once right away (`0x0800E504`) and later by the main loop. | no |
| `:w11=0,0,` | U1, then U2 | PB1 goes **high** while `ui_state != 0x10` (entering PC mode, `0x0800E696/6A0`). Sequence: PB2=1; set `ui_state=0x10`; zero the elapsed timers and loaded-slot numbers; clear `ui_flags` bits 6–9; wait 10 ms; send; wait 10 ms; PB2=0. | yes (each) |
| `:w15=0,0,` | U1, then U2 | PB1 **low** while `ui_state==0x10` (leaving PC mode, `0x0800E6D8/6E2`). Also: PB2=1, `ui_state=1`, clear `pc_flags` bits 6–9, zero all four frequency mirrors, redraw. | yes |
| `:w60=1112111,II,VVVVV,` | U1 | Calibration menu +/− keys change `cal_table[cal_index]` (`0x0800EF90`, `EFEC`, `F04C`, `F0E2`); USART3 `:c` | no |
| `:w61=1114111,` / `:w61=1113111,` | U1 | Calibration menu key 2: if `cal_index==26` → `cal_save(1)` + `…1114111`, otherwise `cal_save(0)` + `…1113111` (`0x0800F104/F112`); USART3 `:d` | no |
| Program sequences (§5.3) | U1 for A, U2 for B | `program_runner_tick` `0x0800B564`, called on every main-loop pass while `ui_state ∉ {0x10, 0xFF}` (`0x0800F192`) | yes (each) |
| Calibration setup sequences (§5.4) | U1/U2 | `cal_apply_test` `0x08011468`: entering calibration mode (`0x0800E54C`) and calibration keys 1/3/4/5 (`0x0800EF1A`, `EF42`, `F142`, `F172`). `cal_apply_test_jig` `0x08011C2C`: USART3 `:b` | yes (each) |
| `:w91=0,` | same port | host `w92` (§3.2) | no |
| Replies `:ok`, `:err`, `:Rev201`, `:nNN=…`, `:pNN=…`, `:gNN=…` | same port | host `g`/`p`/`n` frames (§3.3) | — |

### 5.3 Program runner sequences (VERIFIED, `0x0800B564`)
The layout of `p`/`g` is given in §6.2. For board A (board B uses the same code with the USART2
arrays and flags, from `0x0800B860`):

- **Step.** `step = elapsed / (p[3]*100) + 1`, or `0` if `p[4]==0`. `elapsed` is `g1_elapsed 0x20000024`
  [`g2_elapsed 0x20000028`]; it counts up only while running, once every 101 TIM3 interrupts.
- **End of program** (`step > p[4]`):
  - If repeat mode is off (`ui_flags` bit 8 [9] clear): when running (bit 6 [7]), send
    `:w11=0,0,` (`0x0800B620`); then clear running and set `elapsed=1`.
  - If repeat mode is on: set `elapsed=1`.
- **New step** (`step != sent_step && step != 0`): `g1_step_freq (0x20000050) [g2_step_freq 0x20000058] = p[4+step]`,
  `sent_step = step`, and the gate pair `(g[2*step-2], g[2*step-1])` has any 0xFFFF replaced by 0.
  Then one of two sequences is sent:
  - **Not running** (setup with outputs off), `0x0800B718…7A2`:
    `:w24=<step_freq>,` → `:w11=0,0,` → `:w14=1,` → `:w17=0,1,` → `:w20=<p0>,` → `:w21=<p0>,` →
    `:w28=<p1>,` → `:w29=<p1>,` → `:w32=<p2>,` → `:w33=<p2>,`
  - **Running**, `0x0800B7B2…83C`: `:w24=<step_freq>,` → `:w11=1,1,` → either `:w12=0,0,` (when
    both gate values are 0) or `:w50=<ga>,<gb>,` → `:w51=<ga>,<gb>,` → `:w12=1,1,`.
- **Original behaviour (VERIFIED `0x0800B92A`):** the gate pair for board B is looked up with board
  A's step index (`0x20000060`), not board B's (`0x20000062`).
- Writing 0xFF to `sent_step` (host `g`/`p`, program start/selection keys) forces the next sequence
  to be sent.

### 5.4 Calibration signal sequences (VERIFIED; the meaning of each point is INFERRED)
`cal_apply_test` (menu) and `cal_apply_test_jig` (USART3) send fixed sequences chosen by
`cal_index` (0..21; 28 = all outputs off; 22..27 send nothing).
- The literals used are listed at `0x0801186B…0x08011998` and `0x08012023…0x08012174`: `w11`,
  `w13` (menu only), `w20`/`w21`/`w36`/`w37` (jig only), `w24`/`w25` (for example `3750007`,
  `100000007`, `123456007`), `w28`/`w29` (`60`, `600`, `1200`, `1500`) and `w32`/`w33` (`120`, `220`).
- On points 1, 6, 11 and 16 the frequency and amplitude can be stepped:
  - `0x2000006E` = n (1..9) gives `w24`/`w25 = u64(2^n · 200000.0 + 8.0)`, which is 2^n·20 kHz in
    the §6.1 encoding.
  - `0x2000006F` = m (4..10) gives `w28`/`w29 = u64(10^(m/3))`.
  - The double-precision runtime used: `pow` `0x0801018C`, `dmul` `0x08004CCC`, `dadd` `0x08004760`,
    `ddiv` `0x080047C0`, `d2ulz` `0x08004C08`.

---

## 6. Data formats

### 6.1 Frequency encoding (VERIFIED from the display decoder `0x08005BDC`)
`v = m*10 + e`, where `e = v % 10` and `m = v / 10`. The value is `N = m · 10^e` in units of
**10⁻⁸ Hz**. The screen shows 8 integer digits, a point and 8 decimals, followed by `Hz`.
Examples: `3750007` → 37 500 Hz; `100000007` → 1 MHz; `123456007` → 1 234 560 Hz.
The same encoding applies to `w24`/`w25` and to program `p[5..]`.

### 6.2 Program record (VERIFIED layout, INFERRED semantics)

| Element | Use |
|---|---|
| `p[0]` | sent as `w20`/`w21` (INFERRED waveform) |
| `p[1]` | sent as `w28`/`w29` (INFERRED amplitude) |
| `p[2]` | sent as `w32`/`w33` (INFERRED offset or similar) |
| `p[3]` | dwell per step, in units of 100 elapsed-ticks. `application.tsv` gives a tick as 10.1 ms, so one unit ≈ 1.01 s (INFERRED) |
| `p[4]` | number of steps N, at most 200 (limit used by the dump) |
| `p[5..4+N]` | step frequencies, §6.1 encoding |
| `g[2k], g[2k+1]` | the two `w50`/`w51` arguments of step k+1 (INFERRED gate or burst times) |

- RAM defaults (`0x0800DE2C`, and the blank-slot fallback in `program_load` `0x0800E2E4`):
  `p = {0, 0, 120, 1, 1, 0…}`.
- SPI-flash layout of slot n: `0x10000 + n*0x1000`; p at +0 (0x668 bytes), g at +0x668 (800 bytes).
- Names: sector `0x5000`, 32×60 bytes.
- Calibration: `0x74000`, with a backup copy at `0x75000`.
- SPI flash commands (FYI): read 0x03 `0x0800BCC0`, sector erase 0x20 `0x0800BE9A`, page program
  `0x0800BD14`.

### 6.3 Calibration table `cal_table` `0x2000007E` (u16[32], VERIFIED)
- Initial RW data: `32005, 1662, 316, 496, 1050, 638, 1662, 316, 496, 1000, 638, 1680, 316, 496, 1250,
  638, 1680, 316, 496, 1120, 638, 21, 22, …, 31`.
- Entry `[31]==31` is the validity marker that `0x0800E264` checks before loading from flash.
- Who writes which entries:
  - `[0]`: `w63` (range 31900–32100); INFERRED as the reference trim.
  - `[0..20]`: `w80`.
  - `[idx]`: `w60`, USART3 `:c` and the calibration menu.
  - `[21..28]`: USART3 `:e`…`:l`.

---

## 7. Number helpers (VERIFIED)

| Address | Name | Use |
|---|---|---|
| `0x0800431A` | `__aeabi_idivmod` | Signed; operands here are always non-negative, so it behaves like `__aeabi_uidivmod` (`0x08004306`). r0 = quotient, r1 = remainder. Fixed-width digits in `w_pair5`, `cal_value` and the g-dump. |
| `0x080041C4` | `__aeabi_uldivmod` | 64-bit div/mod. `w_u64`, p-dump, frequency display. |
| `0x08004194` | `__aeabi_lmul` | 64-bit multiply. RX decimal accumulation (`v = v*10 + d`). |
| `0x0800E200` | `u64_pow10_floor` | Largest power of 10 ≤ v (at most 10^16). Used for leading-zero-free output. |
| `0x08012518` | `delay_loop` | Busy loop, n iterations. |

---

## 8. Firmware quirks to reproduce for identical behaviour (VERIFIED)
1. While a port has a frame pending, that port's RX discards bytes, including generator `:ok`
   replies. Host frames must wait for the display's reply.
2. `gen_ok_state` is shared by USART1 and USART2. A timeout is ignored.
3. `.` separates values just like `,`. Digits are not validated (`c & 0x0F`). An empty token means
   all-ones.
4. `idx` is not reset at the start of a frame, only after handling a `w`, `p` or `g` frame; carried
   into a frame of a shorter type it can lie past that array (no range check, see §2.2 step 9).
5. The U2 `w60` handler uses U1's argument array and always zeroes `misc_flags`.
6. Board B's program runner uses board A's step index to look up the gate pair.
7. `:n#` dumps only 200 of the 400 gate values. `:n*` needs `1 ≤ p[4] ≤ 200`.
8. `:n` on U2 with slot 00 targets slot 31. On U1, slot 00 is valid for names only (RAM only).
9. An `n` frame with CR at index ≥ 0x40 is not length-checked afterwards; the equality test is
   `s==0x40`, so the RX buffer can overflow into the next buffer.
10. ORE (overrun) is not handled: the ISR tests only RXNE and OVRDIS is not set. INFERRED risk of an
    interrupt storm on overrun.
11. `w96` runs at most once per boot.

---

## 9. RAM globals owned or changed by the protocol
See the tables in §2.2 and §2.5 and `tools/sim/names/protocols.tsv`. The shared words below are
named in `tools/sim/names/application.tsv`:

| Word | Bits changed by the protocol |
|---|---|
| `ui_flags` `0x2000000E` | 1 = redraw-all; 6/7 = A/B running; 8/9 = A/B repeat; 10/11 = U1/U2 frame pending; 12/13 = A/B channel sync (`w14`); 14 = host access (excluded, §10); 15 = `w96` done |
| `pc_flags` `0x20000010` | 0–3 = frequency fields A1, A2, B1, B2; 4/5 = name A/B; 6–9 = `w15` mirror; bit 10 belongs to an excluded mechanism |
| `misc_flags` `0x20000012` | bit 0 = calibration-menu direction; bits 1–4 = outputs A1, A2, B1, B2 |

---

## 10. Excluded mechanisms
- `0x0800F27C`, `0x0800FA14` (→ `0x08012558`) – one-time flash provisioning command, excluded from scope
- `0x0800F3BC`, `0x0800FB34` – host access-mode toggle command, excluded from scope
- `0x0800F418`, `0x0800FBAA` (→ `0x0800C524`) – reset/bootloader-entry command, excluded from scope
- `0x0800DD4C` – device identity binding check, excluded from scope
- `0x0800DDF0` – bootloader-area flag handler, excluded from scope

---

## 11. Open questions
1. **Physical wiring.** Does the host reach the display through the generator MCUs (forwarding) or
   in parallel on the same lines? PB1 (PC-mode input) and PB2 (output toggled around mode switches
   and by `w92`) need confirming on the board.
2. **Generator-side meaning** of `w11`/`w12`/`w13`/`w14`/`w15`/`w17`/`w20`/`w21`/`w28`/`w29`/
   `w32`/`w33`/`w36`/`w37`/`w50`/`w51`/`w61`/`w91`. Only the display-side use is known. The names
   in §6.2 are INFERRED.
3. **Timing units.** The `gen_wait_ok` timeout and `delay_loop` durations depend on the core clock
   (hardware spec); the values above assume 64 MHz.
4. **Generator reply to `:r80=0,`.** Is it exactly `:w80=13583792,…`? This is deduced only from the
   `w80` handler.
5. **Purpose of `w16`.** It is deferred by the ISR but does nothing in either frame handler.
6. **`w15` mirror bits 6–9.** They have no reader in this image; the screen may not use them.
7. **What the host expects** after `w` frames (the display sends nothing) and after `:n*`/`:n#`
   (one dump line only).
