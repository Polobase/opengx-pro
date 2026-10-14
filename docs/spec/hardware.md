# GX Pro display firmware: hardware and platform map

Scope: MCU platform, clocks, pins, peripherals, internal flash, and the SPI1 device.
The display (LCD drawing), the protocols (UART command sets) and the application logic are
covered in other specs. Cross-references to them are given by address.

Conventions
- Addresses are code addresses (Thumb bit cleared) unless noted. `FUN_xxxxxxxx` is the function
  at that address in the disassembly of the original image. New names are in
  `tools/sim/names/hardware.tsv`.
- **[V]** = VERIFIED: read directly from the code (decompiled output checked against capstone
  disassembly wherever the decompiler could be wrong).
  **[I]** = INFERRED: reasoning, conventions, or datasheet knowledge; not proven by the code.
- LL = STM32Cube G0 Low-Layer driver, HAL = STM32Cube G0 HAL.

---

## 1. Toolchain, libraries, runtime

### 1.1 What is linked [V]
- **Keil ARMCC standard C library** (not microlib): `__main`/`__scatterload`/`__rt_entry`,
  `__user_setup_stackheap`, `__user_libspace` (libspace at 0x20002670), `_sys_exit` that
  uses semihosting `BKPT 0xAB` (0x0800521C). `main` never returns, so exit is never reached.
- **HAL** only for the system parts: HAL_Init, tick, RCC, PWR, FLASH/FLASHEx (program and
  option bytes), CORTEX (NVIC priority, SysTick), SYSCFG.
- **LL** for every application peripheral: GPIO, SPI, TIM, USART, and the RCC clock queries LL
  needs. There are **no** HAL_GPIO_*, HAL_UART_*, HAL_SPI_*, HAL_TIM_*, HAL_Delay,
  HAL_FLASHEx_Erase, memcpy or printf-family functions in the image.
- Double-precision soft-float (Keil fplib plus mathlib `pow`/`sqrt`). No single-precision float
  helpers are present.

### 1.2 Memory layout [V]
| Item | Value | Evidence |
|---|---|---|
| Vector table | 0x08004000, 46 entries (16 + 30 IRQ) | image header; `SystemInit` 0x0800C5F4 sets VTOR=0x08000000, `main` 0x0800E404 sets VTOR=0x08004000 |
| RW data | 0x20000000..0x200000CF, copied from 0x080162E0 | `__scatterload` |
| ZI | 0x200000D0..0x20002CCF | scatter table |
| Keil libspace (errno at +0) | 0x20002670..0x200026CF | `__user_libspace` 0x08005124, `__aeabi_errno_addr` 0x08004F8C |
| Heap | 0x200026D0..0x200028CF (512 B, unused) | `__user_initial_stackheap` 0x08004170 returns r0=0x200026D0, r1=0x20002CD0, r2=r3=0x200028D0 |
| Stack | 0x200028D0..0x20002CCF (1 KB), initial SP 0x20002CD0 | same, vector[0] |

Enabled vectors [V]: TIM3 (IRQ16) → 0x0800C604, USART1 (IRQ27) → 0x0800C758, USART2 (IRQ28)
→ 0x0800CF90, USART3_4 (IRQ29) → 0x0800D7D8, SysTick → 0x0800C55C (calls HAL_IncTick). NMI,
SVC and PendSV are empty returns. HardFault and every other IRQ loop forever.

Global interrupt enable [V]: the image contains no `cpsie i`. The only PRIMASK use is the
save/`cpsid`/restore in `FLASH_Program_Fast` (0x080090C8), which is never called. So the
**app relies on the bootloader jumping to it with PRIMASK = 0**.

### 1.3 Runtime / compiler helper functions
All names are **[I]** by structure unless marked. The callers listed are the evidence.

| Address | Name | Notes |
|---|---|---|
| 0x08004134 / 0x08004138 | `__rt_lib_init` / `__rt_lib_shutdown` | empty |
| 0x0800414E | `__rt_exit` | calls `__rt_lib_shutdown` and `_sys_exit` |
| 0x08004170 | `__user_initial_stackheap` | [V] returns the values in 1.2 |
| 0x08004194 | `__aeabi_lmul` | 64×64→64 multiply |
| 0x080041C4 | `__aeabi_uldivmod` | 64-bit unsigned divide (used with divisor 10 for number formatting) |
| 0x080042C6 / 0x080042E0 | `_memset_w` / `_memset` | internal fill loops (r2 = fill word) |
| 0x080042FE / 0x08004302 | `__aeabi_memclr` / `__aeabi_memclr4` | [V] `movs r2,#0; b _memset(_w)` |
| 0x08004306 | `__aeabi_uidivmod` | [V] used as uint divide by HAL (`1000/uwTickFreq`, `16000000>>…`) |
| 0x0800431A | `__aeabi_idivmod` | signed divide, remainder in r1 (digit extraction `/10`, `/100`…) |
| 0x08004468 / 0x0800458A | `_dadd_core` / `_dsub_core` | same-sign magnitude add / subtract |
| 0x08004760 / 0x0800477A / 0x08004790 | `__aeabi_dadd` / `__aeabi_dsub` / `__aeabi_drsub` | [V] sign dispatch into the cores |
| 0x080047C0 | `__aeabi_ddiv` | [V] `1.0/0.0` in `__mathlib_dbl_divzero` |
| 0x08004BF0 | `__aeabi_drdiv` | [V] swaps its operands and calls ddiv |
| 0x08004C08 | `__aeabi_d2ulz` | double → uint64 (0 if negative) |
| 0x08004C74 | `_dflt_normalize` | shared tail of i2d/ui2d |
| 0x08004CB6 / 0x08004CC6 | `__aeabi_i2d` / `__aeabi_ui2d` | |
| 0x08004CCC | `__aeabi_dmul` | |
| 0x08004F1E | `__set_errno` | stores to `*__aeabi_errno_addr()` |
| 0x08004F2C | `__ARM_scalbn` | used by pow and by overflow/underflow helpers |
| 0x08004F8C | `__aeabi_errno_addr` | returns 0x20002670 |
| 0x08004F94 | `__fpl_dcmp_InfNaN` | NaN path of the double compare |
| 0x08005024 | `_dsqrt` | |
| 0x08005124 | `__user_libspace` | returns 0x20002670 |
| 0x0800512C | `__user_setup_stackheap` | |
| 0x0800516A | `exit` | |
| 0x0800517A / 0x080051A8 / 0x080051BC | `__fpl_cmpreturn` / `__fpl_return_NaN` / `__fpl_exception_result` | fplib internals |
| 0x0800521C | `_sys_exit` | [V] `r0=0x18, r1=0x20026, BKPT 0xAB` (semihosting) |
| 0x0800D9C8 | `__ARM_common_switch8` | [V] byte jump-table helper (see 7.3) |
| 0x0800D9E4 | `__ARM_fpclassify` | returns 0 zero, 3 inf, 4 subnormal, 5 normal, 7 NaN |
| 0x0800DB90 | `__kernel_poly` | Horner evaluation for pow |
| 0x0800DC3C..0x0800DC7C | `__mathlib_dbl_divzero/infnan2/invalid/overflow/underflow` | pow error returns |
| 0x0801018C | `pow` | [V] fdlibm constants (0x3FEEC709E0000000 = cp_h, 1/3, 0.5, 1024, −1075) |
| 0x08012490 | `sqrt` | `_dsqrt` plus errno=1 (EDOM) for negative input |
| 0x08012580 | `__aeabi_cdrcmple` | flag-returning double compare (pow only) |

`pow` is used only by 0x08011468 and 0x08011C2C (application code: frequency values from
exponents) → application-logic spec.

### 1.4 HAL functions
The names are **[I]**; each body was compared against the HAL source structure.

| Address | Name | Key behaviour seen [V] |
|---|---|---|
| 0x08008FEA | `Error_Handler` | empty. Errors from SystemClock_Config are ignored |
| 0x08008FEC | `FLASH_OB_GetRDP` | OPTR&0xFF; not 0xAA/0xCC → 0xBB |
| 0x08009008 | `FLASH_OB_GetUser` | OPTR & 0x074F6000 |
| 0x08009020 / 0x08009060 / 0x08009080 | `FLASH_OB_GetWRP` / `FLASH_OB_OptrConfig` / `FLASH_OB_WRPConfig` | |
| 0x080090A0 | `FLASH_Program_DoubleWord` | CR.PG=1, word, ISB, word+4 |
| 0x080090C8 | `FLASH_Program_Fast` | linked, unused |
| 0x0800911C | `FLASH_WaitForLastOperation` | BSY (SR bit16) / CFGBSY (bit18) with a HAL_GetTick timeout; clears SR errors with 0x83FB |
| 0x08009304 | `HAL_FLASHEx_OBGetConfig` | |
| 0x08009330 | `HAL_FLASHEx_OBProgram` | sets OPTSTRT (CR bit17) |
| 0x080093F8 / 0x08009504 | `HAL_FLASH_Lock` / `HAL_FLASH_Unlock` | KEYR 0x45670123, 0xCDEF89AB |
| 0x08009434 / 0x08009458 | `HAL_FLASH_OB_Lock` / `HAL_FLASH_OB_Unlock` | OPTKEYR 0x08192A3B, 0x4C5D6E7F |
| 0x0800941C | `HAL_FLASH_OB_Launch` | CR bit27 OBL_LAUNCH, which resets the MCU |
| 0x0800948C | `HAL_FLASH_Program` | lock byte at 0x20002658, ErrorCode at 0x2000265C |
| 0x08009538 | `HAL_GetTick` | returns uwTick (0x200000C0) |
| 0x08009544 / 0x08009550 / 0x0800955C | `HAL_GetUIDw0/1/2` | 0x1FFF7590 / 94 / 98 |
| 0x08009568 | `HAL_IncTick` | uwTick += uwTickFreq (0x200000C8, init 1) |
| 0x08009580 | `HAL_Init` | FLASH_ACR |= PRFTEN (0x100); HAL_InitTick(0); HAL_MspInit |
| 0x080095AC | `HAL_InitTick` | SysTick = SystemCoreClock/(1000/uwTickFreq); priority stored in uwTickPrio 0x200000C4 (RW init 4) |
| 0x0800960C | `HAL_MspInit` | enables SYSCFG and PWR clocks; `HAL_SYSCFG_StrobeDBattpinsConfig(0x600)` |
| 0x08009658 | `HAL_NVIC_SetPriority` | |
| 0x0800966C | `HAL_PWREx_ControlVoltageScaling` | |
| 0x080096C4 | `HAL_RCCEx_PeriphCLKConfig` | |
| 0x080098C8 | `HAL_RCC_ClockConfig` | |
| 0x08009A9C | `HAL_RCC_GetSysClockFreq` | [V] HSI_VALUE 16 MHz, **HSE_VALUE 8 MHz** |
| 0x08009B80 | `HAL_RCC_OscConfig` | HSE timeout 100 ms, PLL timeout 2 ms |
| 0x0800A134 | `HAL_SYSCFG_StrobeDBattpinsConfig` | SYSCFG_CFGR1 bits 9/10 |
| 0x0800A14C | `HAL_SYSTICK_Config` | CSR=7 (processor clock, IRQ, enable) |
| 0x0800C524 | `NVIC_SystemReset` | AIRCR=0x05FA0004. Called only from the USART1/USART2 command handlers (bl at 0x0800F428, 0x0800FBBA) for register 95 with value 0x2EF5 (12021), i.e. `:w95=12021` [command syntax I] |
| 0x0800DA10 | `NVIC_EnableIRQ` | |
| 0x0800DA28 / 0x0800DAA0 / 0x0800DB18 | `NVIC_SetPriority_*` | three static-inline copies (tim.c, usart.c, hal_cortex) |
| 0x0800C564 | `SystemClock_Config` | see section 2 |

LL functions (all **[I]** by name, register effect **[V]**): `LL_GPIO_Init` 0x0800A7D8,
`LL_GPIO_ResetOutputPin` 0x0800A8D2 (BRR), `LL_GPIO_SetOutputPin` 0x0800A8D6 (BSRR),
`LL_IOP_GRP1_EnableClock` ×3 (0x0800A8DC/0x0800A8F8/0x0800A914), `LL_APB1_GRP1_EnableClock`
0x0800A7BC, `LL_SPI_Init` 0x0800AA78, `LL_SPI_Enable` 0x0800AA6E, `LL_SPI_SetStandard` 0x0800AAFC,
`LL_SPI_DisableNSSPulseMgt` 0x0800AA64, `LL_SPI_IsActiveFlag_BSY` 0x0800AAF0,
`LL_SPI_TransmitData8` 0x0800AB08, `LL_TIM_Init` 0x0800AB0C, `LL_USART_Init` 0x0800ABF4,
`LL_USART_SetBaudRate` 0x0800AD00 (table `USART_PRESCALER_TAB` at 0x08016120 = 1,2,4,6,8,10,12,16,32,64,128,256),
`LL_USART_ConfigAsyncMode` 0x0800ABB4, `LL_USART_DisableFIFO` 0x0800ABC8,
`LL_USART_SetTX/RXFIFOThreshold` 0x0800AD88/0x0800AD78, `LL_USART_Enable` 0x0800ABD4,
`LL_USART_EnableIT_RXNE_RXFNE` 0x0800ABEA, `LL_USART_EnableIT_PE` 0x0800ABDE,
`LL_USART_IsActiveFlag_TEACK/REACK/RXNE_RXFNE` 0x0800ACE2/0x0800ACC8/0x0800ACD6,
`LL_USART_IsEnabled` 0x0800ACF0, `LL_USART_TransmitData8` 0x0800AD96, and RCC queries
0x0800A930, 0x0800A94C, 0x0800AA08..0x0800AA50, 0x0800BB78..0x0800BC24
(AHBPrescTable 0x08016150, APBPrescTable 0x08016190).

Consequence [I]: `LL_USART_Init` and `LL_SPI_Init` do nothing if the peripheral is already
enabled (UE=1 / SPE=1). The app never resets its peripherals, so it expects the bootloader to
hand over USART1/2/3, SPI1/2 and TIM3 disabled, or at reset state.

---

## 2. Clock tree
Source: `SystemClock_Config` 0x0800C564, checked against its disassembly. All values in this
section are [V] unless marked.

| Item | Value |
|---|---|
| Voltage scaling | Range 1 (`HAL_PWREx_ControlVoltageScaling(0x200)`) |
| Oscillator | **HSE ON** (crystal, not bypass: HSEState=0x10000). HSE_VALUE compiled as **8 MHz**. GPIOF clock is enabled in MX_GPIO_Init; PF0/PF1 are OSC_IN/OSC_OUT [I] |
| PLL | source HSE, M=/1, N=16 → VCO 128 MHz; R=/2 → **64 MHz** (PLLREN=1); P=/2 set in the register but PLLPEN=0 (unused); no Q on G070 |
| SYSCLK | PLLRCLK = **64 MHz** |
| HCLK | 64 MHz (AHB /1) |
| PCLK (APB) | 64 MHz (APB /1) → timer clock 64 MHz |
| Flash latency | **2 WS**. Prefetch enabled by HAL_Init. ICEN is left at its reset value 1 (ACR reset 0x600) |
| USART1/USART2 kernel clock | PCLK (CCIPR USART1SEL=USART2SEL=0 via `HAL_RCCEx_PeriphCLKConfig`, PeriphClockSelection=3). USART3 always uses PCLK |
| SystemCoreClock | 0x200000CC: initial 16 000 000 (RW data), 64 000 000 after ClockConfig |
| SysTick | 1 kHz (reload 63999), priority 0; used only by HAL timeouts (flash, RCC) |
| MCO, LSE, LSI, RTC | not used |

Failure behaviour [I]: if HSE does not start, `Error_Handler` returns and the clock switch to the
PLL fails, so the MCU keeps running on HSI 16 MHz. LL recomputes the baud rates from the real
clock, so the UARTs still work; every busy-wait delay becomes 4× longer.

### 2.1 Busy-wait delay `delay_loop` 0x08012518 [V code, I timing]
```
nop ; loop: subs r0,#1 ; cmp r0,#0 ; bne loop
```
The application has no timer-based delays. Every delay is this loop. The constants are chosen
as if one iteration took 3 cycles (64e6/3 per second): 0x34155=213333 → "10 ms",
0x682AA → "20 ms", 0x82355 → "25 ms", 640000 → "30 ms", 0x271000 → "120 ms",
64 000 000 → "3 s", 0xD5 → "10 µs", 0x855 → "100 µs".
On a Cortex-M0+ with the flash cache hit, one iteration is more likely **4 cycles**, so the real
times are about 4/3 of the intended ones (10 ms → ≈13.3 ms). **Measure this on hardware** if
exact timing matters. A reimplementation should use the same constants divided by
iterations/µs, or replace them with real timers deliberately.

The helper at 0x08012524, which waits for an "ok" reply over UART, is **mis-decompiled**: the
decompiler dropped the loop exit. The disassembly shows it clears 0x2000000B, then polls up to 200000
iterations for `*(u8*)0x2000000B == 2` (set by the USART1/USART2 IRQs on receiving "ok"), then
runs `delay_loop(0x855)`. See the protocols spec.

---

## 3. Pin map
Source: `MX_GPIO_Init` 0x0800AD9C (verified instruction by instruction, LL_GPIO_InitTypeDef at
sp+4: Pin, Mode, Speed, OutputType, Pull, Alternate=0), plus MX_SPI1/2_Init and
MX_USARTx_Init. Speed codes: L=low, M=medium, H=high, VH=very high. The initial level is written
before the pin is switched to output (Set/ResetOutputPin calls at 0x0800ADC6..0x0800AE0A).

| Pin | Config [V] | Init level [V] | Role | Runtime accesses [V] |
|---|---|---|---|---|
| PA1 | out PP VH, no pull | high | **SPI1 flash CS#** (active low) [V by use] | BRR/BSRR bit1 in 0x0800BCC0, 0x0800BE38, 0x0800BE9A, 0x0800BF3C, 0x0800BF6A |
| PA2 | AF1 PP H, no pull | – | USART2_TX | |
| PA3 | AF1 PP H, pull-up | – | USART2_RX | |
| PA5 | AF0 PP VH | – | SPI1_SCK | |
| PA6 | AF0 PP VH | – | SPI1_MISO | |
| PA7 | AF0 PP VH | – | SPI1_MOSI | |
| PA8 | out PP VH, no pull | high | **LCD CS#** [V by use] | BRR/BSRR 0x100 in lcd_fill_rect 0x080091AC, 0x0800D8D8, 0x0800D938, 0x0800D980, 0x0800A188 |
| PA9 | AF1 PP H, no pull | – | USART1_TX | |
| PA10 | AF1 PP H, pull-up | – | USART1_RX | |
| PB0 | out PP L, **pull-down** | low | unknown; never driven high (main 0x0800E430 writes BRR=1 again) | only resets |
| PB1 | in, pull-up | – | **external control/status input** [I]: low = the display runs the generators (state 1); high = idle/remote state 0x10, and the storage and calibration commands are refused unless unlocked | IDR bit1 in main_loop (0x0800E55E…), USART1_IRQHandler, USART2_IRQHandler, 0x0800F22C, 0x0800F9C4 |
| PB2 | out PP L, no pull | low | **control/handshake output** [I]: high while the display drives the generators; toggled forever after the long-press power-off (half period `delay_loop(0xD5)`: nominally 10 µs → 50 kHz, probably about 13 µs → about 37 kHz) | BSRR/BRR bit2: main 0x0800E546 (service-mode boot), main_loop, 0x0800F22C/0x0800F9C4 (register 91 → low, register 92 → high) |
| PB3 | in, pull-up | – | key → code 6; with PD2 → code 8 | keys_scan 0x0800E0F8 |
| PB4 | in, pull-up | – | key → code 4 | keys_scan |
| PB5 | in, pull-up | – | key → code 3; with PB11 → code 10 | keys_scan |
| PB6 | in, pull-up | – | **not read anywhere** | – |
| PB7 | out PP L, no pull | low | soft-PWM output, duty `*(u8*)0x2000000C` /16 (load unknown: LED or fan [I]) | TIM3_IRQHandler 0x0800C604 |
| PB8 | AF4 PP L, no pull | – | USART3_TX | |
| PB9 | AF4 PP L, pull-up | – | USART3_RX | |
| PB10 | in, pull-up | – | **not read anywhere** | – |
| PB11 | in, pull-up | – | key → code 9 (long press → code 0x19 = power-off) | keys_scan |
| PB12 | out PP L, no pull | low | **LCD backlight**, soft-PWM, duty `*(u8*)0x2000001B` /16 [I role, V PWM] | TIM3_IRQHandler; direct BSRR/BRR in 0x080065D8 and main_loop |
| PB13 | AF0 PP VH | – | SPI2_SCK → LCD SCL | |
| PB15 | AF0 PP VH | – | SPI2_MOSI → LCD SDA (PB14/MISO not configured) | |
| PC6 | out PP VH, no pull | low | **LCD D/C** (0 = command, 1 = data) | 0x0800D980 clears it (command), 0x0800D938/0x0800D8D8/lcd_fill_rect set it (data) |
| PC7 | out PP H, no pull | high | **LCD RESET#** | 0x0800A188: low, delay 0x682AA, high, delay 0x682AA |
| PD0 | in, pull-up | – | key → code 1 | keys_scan |
| PD1 | in, pull-up | – | key → code 2 | keys_scan |
| PD2 | in, pull-up | – | key → code 7; with PB3 → code 8 | keys_scan |
| PD3 | in, pull-up | – | key → code 5 | keys_scan |
| PF0/PF1 | reset state (clock enabled) | – | HSE crystal [I] | – |
| PA13/PA14 | reset state | – | SWD [I] | – |

The literal-pool scan over the whole code range found no GPIO/peripheral base loads outside the
functions listed here [V], so no other pins are touched.

### 3.1 Key scan `keys_scan` 0x0800E0F8 [V]
Keys are active low with internal pull-ups. The scan always runs `delay_loop(0x34155)`
(debounce). The intended skip-when-idle test compares `((IDR_B<<1)&0x70)|(IDR_D&0xF)` with
0xFF, a value it can never reach (maximum 0x7F), so **the delay runs on every call**. Then it
checks in this priority order, returning the first match:

| Order | Condition (low = pressed) | Return |
|---|---|---|
| 1 | PB3 && PD2 | 8 |
| 2 | PB11 && PB5 | 10 |
| 3 | PD0 | 1 |
| 4 | PD1 | 2 |
| 5 | PB5 | 3 |
| 6 | PB4 | 4 |
| 7 | PD3 | 5 |
| 8 | PB3 | 6 |
| 9 | PD2 | 7 |
| 10 | PB11 | 9 |
| – | none | 0 |

Code 8 at boot enters the service/calibration mode (main 0x0800E4BC..0x0800E50C). Hold detection
is done by TIM3 (section 4.3) and main_loop (application-logic spec).

### 3.2 SYSCFG dead-battery strobes [V value, I meaning]
`HAL_MspInit` sets SYSCFG_CFGR1 UCPD1_STROBE|UCPD2_STROBE (0x600). On the G0 die this
disconnects the UCPD dead-battery pull-downs on PA8/PB15 (UCPD1) and PD0/PD2 (UCPD2): the LCD CS,
the LCD MOSI and two key inputs. **A reimplementation must do the same**, or those pins keep a
weak pull-down.

---

## 4. Peripheral configuration

### 4.1 Summary table
| Peripheral | Configuration [V] | IRQ / NVIC [V] | Used by |
|---|---|---|---|
| USART1 | PA9/PA10 AF1; 115200 (BRR=556 → 115108 Bd, −0.08 %), 8N1, oversampling 16, prescaler /1, TE+RE, no flow control, FIFO disabled, async mode; CR1 = UE\|RE\|TE\|RXNEIE\|PEIE | IRQ27, priority 0; RXNEIE+PEIE set in main | RX: USART1_IRQHandler 0x0800C758. TX: `uart1_putc` 0x08010B88 (writes TDR, waits for TC) |
| USART2 | PA2/PA3 AF1; identical settings | IRQ28, priority 0 | USART2_IRQHandler 0x0800CF90; `uart2_putc` 0x08010BA8 |
| USART3 | PB8/PB9 AF4; identical settings (FIFO thresholds not written and FIFOEN left at reset value 0) | IRQ29 (USART3_4), priority 0 | USART3_4_IRQHandler 0x0800D7D8 (USART3 only); `uart3_putc` 0x08010BC8, called only from `usart3_cmd_dispatch` 0x0800522C (reply byte 'Z') |
| USART4 | not used (only referenced inside LL_USART_Init) | – | – |
| SPI1 | PA5/6/7 AF0; master, full duplex, **mode 3** (CPOL=1, CPHA=1), 8-bit, MSB first, software NSS (SSM=SSI=1), **/2 → 32 MHz**, Motorola, no CRC; CR1=0x0307(+SPE); CR2: DS=8 bit, FRXTH=1 (set in main 0x0800E47A) | none (polled) | external SPI NOR flash (section 6) |
| SPI2 | PB13/PB15 AF0; same settings as SPI1 (mode 3, /2 → 32 MHz), FRXTH not set, RX never read | none (polled) | LCD (ST7789-type) write-only bus. Every byte: wait BSY=0 (`LL_SPI_IsActiveFlag_BSY`), write DR (8-bit). See display spec |
| TIM3 | PSC=63, ARR=99, up-counting, clock division /1 (CKD=00), ARPE=1, internal clock, TRGO reset, no master/slave → **10 kHz update (100 µs)**; UIE and CEN set in main (0x0800E438) | IRQ16, priority 0 | TIM3_IRQHandler 0x0800C604 (section 4.3) |
| TIM1/14/15/16/17 | not used (only pointer comparisons inside LL_TIM_Init) | – | – |
| SysTick | 1 ms, priority 0 | – | HAL_IncTick only |
| DMA, ADC, I2C, RTC, EXTI, IWDG, WWDG, CRC | **not used**: no register access anywhere | – | – |
| FLASH | program doubleword + option bytes (section 5) | FLASH IRQ unused | 0x0800BC84, 0x0800DD4C, 0x0800DDF0, 0x08012558 |

RCC enables [V]: IOPENR GPIOA/B/C/D/F. APBENR1: TIM3 (bit1), SPI2 (14), USART2 (17), USART3 (18),
PWR (28). APBENR2: SYSCFG (0), SPI1 (12), USART1 (14).

NVIC [V]: TIM3, USART1, USART2, USART3_4 and SysTick all have priority 0 (`(0&3)<<6`), so
nothing preempts anything. When several are pending at once, the lower exception number runs
first: SysTick, then TIM3, then USART1, then USART2, then USART3_4.

### 4.2 UART receive handling notes (for the protocols spec)
- All three RX IRQs check only RXNE (ISR bit5, 0x0800ACD6) and read RDR. **No handler clears ORE,
  FE, NE or PE** (no ICR write anywhere [V]). OVRDIS is 0 and RXNEIE also enables the overrun
  interrupt, so an overrun leaves the IRQ re-triggering with RXNE=0: the main loop stalls while
  TIM3 keeps running [I]. A reimplementation should clear ORE (compatible behaviour otherwise).
- TX is blocking: `uartN_putc` writes TDR and busy-waits TC (ISR bit6). It does not check TXE
  first.

### 4.3 TIM3 interrupt (10 kHz) [V from disassembly]
```
TIM3_SR = ~1                                   // clear UIF
tick8 (u8 @0x20000008) += 1                   // wraps 256 → 25.6 ms period
PB7  = ((tick8 & 15) < duty7  (u8 @0x2000000C)) ? 1 : 0          // 16-step PWM, 625 Hz
sub101 (u8 @0x2000000A): if sub101 > 99 → 0 else +1               // period 101 ticks = 10.1 ms
if sub101 == 50: if flags(0x2000000E)&0x40 → (u32@0x20000024)++ ; if &0x80 → (u32@0x20000028)++
PB12 = ((tick8 & 15) < bl_duty (u8 @0x2000001B)) && !(flags&1) ? 1 : 0    // backlight PWM
if key(u8 @0x2000001A) > 0x10: if tick8 == 0 → hold (u8 @0x20000009) = min(hold+1, 255)
else hold = 0                                                     // hold counter in 25.6 ms units
if tick8 == 100: text-scroll counter update (0x20000018, 0x20001C88, flag 0x20) – display spec
```
- Backlight duty: 0 at reset, set to 8 (50 %) by main after the LCD init and the first clear
  (0x0800E4B2), and by the power-off path in main_loop. Register 64 in 0x0800F22C/0x0800F9C4
  sets 8 (value 0x12038B1) or 0 (value 0x989681). Direct PB12
  BSRR/BRR writes elsewhere (0x080065D8, main_loop) are overridden by the next ISR tick (≤100 µs),
  so they have almost no visible effect [I].
- PB7 duty: set to 1 by 0x0800DE2C (init), and by USART3 command 'b' in
  `usart3_cmd_dispatch` (0x080052A6): value ≤5 → 1, 6..10 → 6, 11..15 → 10, 16..20 → 15,
  otherwise 1.
- The 0x20000024/0x20000028 counters advance once per 101 TIM3 ticks (10.1 ms, not 10 ms)
  [V: `cmp #0x63; ble` then increment]. This affects any treatment-time display that uses them
  (application-logic spec).

### 4.4 Boot / bring-up order in `main` 0x0800E404 [V]
1. VTOR = 0x08004000
2. HAL_Init, SystemClock_Config
3. MX_GPIO_Init, MX_USART1, MX_SPI1, MX_SPI2, MX_TIM3, MX_USART2, MX_USART3
4. PB0 := 0. TIM3 DIER.UIE and CR1.CEN on. USART1/2/3: RXNEIE+PEIE. SPI1 FRXTH=1. SPI1/SPI2 SPE=1
5. `delay_loop(0x82355)`, LCD reset and init 0x0800A188 (panel variant from 0x08003C00, section 5)
6. 0x0800DE2C: load the name table and settings from SPI flash, init the channel records, PB7 duty=1
7. clear the LCD, backlight duty := 8 (0x0800E4B2), splash 0x080054F4
8. `keys_scan()==8` (0x0800E4BC) → service mode: state 0x2000001C := 0xFF; wait for all keys
   released (each poll includes the debounce delay); send `:r80=0,` on USART1; clear the LCD;
   0x0800F22C if flag 0x2000000E bit10 is set; 0x0800DED4. Otherwise wait up to
   100 × `delay_loop(640000)` for a USART3 line (flag 0x20000014 bit0), loop at 0x0800E516
9. if state == 0xFF (service mode): PB2 := 1 (0x0800E546), then 0x08011468
10. `flash_settings_reset_if_flagged` 0x0800DDF0, `flash_uid_binding_check` 0x0800DD4C,
    `flash_ensure_rdp_level1` 0x0800BC84 (may reset the MCU), `main_loop`.

Note: step 6 loads the settings **before** step 10 may erase them in SPI flash. RAM keeps the
loaded copy until the next boot.

---

## 5. Internal flash usage [V]
The app **never erases** internal flash (no PER/STRT/MER writes; no HAL_FLASHEx_Erase).
`HAL_FLASH_Program` (TypeProgram=1 = DOUBLEWORD) has exactly three callers, and all of them
target the bootloader's last 2 KB page (page 7, 0x08003800..0x08003FFF). Each field is a
write-once doubleword.

| Address | Size | Accessed by | Meaning / behaviour |
|---|---|---|---|
| 0x08003C00 | u16 (`ldrh` 0x0800A1AE), read | LCD init 0x0800A188 | **LCD panel variant** [V, re-checked]: 0xFF00 or 0xF000 → variant A; 0xFFF0 **or any other value (incl. erased 0xFFFF)** → variant B (full init, default path 0x0800A638). There is no 'no init' case. Sequences: display spec §3. Written outside this image (bootloader/factory) [I] |
| 0x08003D00 | u64 | `flash_settings_reset_if_flagged` 0x0800DDF0 | **settings-reset request**: if the doubleword ≠ 0 (erased = 0xFF…FF counts as ≠ 0): erase SPI-flash sectors 0x74000 and 0x75000, then program {0,0}. So after page 7 is erased (fresh chip or bootloader action [I]) the stored settings are wiped once |
| 0x08003E00 | u64 | `flash_uid_binding_check` 0x0800DD4C | Vendor device-binding check. **Excluded from scope**: the open firmware has no device-binding check, and this mechanism is not documented here. The open firmware never writes this area. |
| 0x08003F00 | u64 | `flash_write_word_3f00` 0x08012558 | if the first word is 0xFFFFFFFF → program {value, 0}. The value comes from register 00 (`:w00=<n>` [syntax I]) in the USART1/USART2 command handlers (bl at 0x0800F282, 0x0800FA1A). **The app never reads it**; presumably the bootloader uses it (serial number or licence) [I] |

Option bytes, `flash_ensure_rdp_level1` 0x0800BC84 [V]: unlock the flash and the option bytes,
then `HAL_FLASHEx_OBGetConfig(&OBInit @0x200000D0)`. If RDPLevel (0x200000E0) < 0xBB (i.e. RDP
level 0 = 0xAA): OptionType=OPTIONBYTE_RDP, RDPLevel=0xBB (level 1), `HAL_FLASHEx_OBProgram`
(which keeps the user option bits), then `HAL_FLASH_OB_Launch`, which reloads the option bytes
and resets. Finally lock both. **On its first boot the stock firmware makes the chip
read-protected (RDP level 1).** No WRP, PCROP or user-option (watchdog, BOOT) changes.

---

## 6. SPI1 device: external SPI NOR flash [V commands, I part]
Chip select PA1 (active low), SPI1 mode 3, 32 MHz, 24-bit addresses, 256-byte pages,
4 KB sectors. The commands are the standard 25-series (W25Qxx-compatible) set. **No JEDEC-ID or
status-register writes are issued**, so the part cannot be identified from the code. The highest
address used is 0x75040, so the chip holds at least 512 KB [I].

Byte transfer `spi1_xfer` 0x0800BEE0: wait TXE (SR bit1) for at most 101 polls, write DR as a
byte, wait RXNE (SR bit0) for at most 101 polls, return DR&0xFF. Timeouts fall through
silently.

| Function | Sequence (CS low … CS high) |
|---|---|
| `spiflash_write_enable` 0x0800BF6A | `06` |
| `spiflash_wait_ready` 0x0800BF3C | `05`, then send `FF` until (status & 1) == 0 (no timeout) |
| `spiflash_read` 0x0800BCC0 (buf, addr, len u16) | `03 A23..16 A15..8 A7..0`, then `len` × send `FF`/store the byte |
| `spiflash_page_program` 0x0800BE38 (buf, addr, n≤256) | WREN; `02 A2 A1 A0 data…`; then wait_ready |
| `spiflash_sector_erase` 0x0800BE9A (addr) | WREN; wait_ready; `20 A2 A1 A0`; wait_ready |
| `spiflash_write` 0x0800BD14 (buf, addr, len) | splits into page programs at 256-byte page boundaries (first chunk up to the page end, full pages, remainder). **No erase inside**: callers erase first |

### 6.1 SPI-flash storage map [V addresses, I semantics]
| SPI address | Size used | RAM mirror | Loader / saver | Content |
|---|---|---|---|---|
| 0x005000 | 0x780 (1920 B) | 0x200001F8..0x20000977 | `store_names_load` 0x0800E29C (0xFF bytes → 0) / `store_names_save` 0x08011380 (erase, entries 0 and 31 written as zeros) | 32 × 60-byte text entries (program names). Erased (with slots 1..30) by the register-96 value 0x3021 (12321) clear-all path at 0x0800F42C, at most once per boot (flag 0x2000000E bit15) |
| 0x010000 + n×0x1000 | 0x988 (2440 B) | ch1: 0x20000978 (0x668) + 0x20001648 (0x320); ch2: 0x20000FE0 + 0x20001968 | `store_program_load` 0x0800E2E4(n, ch) / `store_program_save` 0x080113EC(n, ch) | program slot n (n = 1..30 accepted by the command handlers; n=1..30 erased by clear-all). Layout: u64[5] header + u64[200] values at +0x000 (0x668 B); u16[400] at +0x668 (800 B). Load check: header[4] (count) must be 1..200 and header[3] ≠ 0, otherwise defaults {0,0,120,1,1, zeros}. Meaning: application-logic spec |
| 0x074000 | 0x40 | 0x2000007E..0x200000BD (32 × u16) | `store_settings_load` 0x0800E264 (accepted only if word[31] == 0x001F) / `store_settings_save` 0x08011310(backup) (erase and write only if different; also to 0x75000 when backup≠0) | settings / calibration block. Defaults in RW data: 32005, 1662, 316, 496, 1050, 638, 1662, 316, 496, 1000, 638, 1680, 316, 496, 1250, 638, 1680, 316, 496, 1120, 638, 21..31 |
| 0x075000 | 0x40 | – | `store_settings_restore_backup` 0x08011288 (if word0 ≠ 0xFFFF: copy to RAM and rewrite 0x74000 if it differs) | backup / factory copy of the settings block |

Both 0x74000 and 0x75000 are erased (not rewritten) at boot when the 0x08003D00 flag is set
(section 5).

---

## 7. Cross-references and decompiler caveats

### 7.1 For the display spec
- LCD bus: SPI2 write-only, CS=PA8, D/C=PC6, RESET=PC7, backlight=PB12 (TIM3 soft-PWM, duty
  0x2000001B, 0..16). Low-level functions: command byte 0x0800D980, data byte 0x0800D938, data
  u16 0x0800D8D8, window 0x0800C4EC (0x2A/0x2B/0x2C), fill 0x080091AC. Init 0x0800A188 selects
  one of two variants from the u16 at 0x08003C00 (see section 5). Reset pulse: low for `delay_loop(0x682AA)`,
  then high for `delay_loop(0x682AA)`.

### 7.2 For the protocols spec
- USART1 and USART2 are identical 115200 8N1 links with command handlers 0x0800F22C / 0x0800F9C4.
  USART3 (PB8/PB9) receives `:`, then a letter `a`..`l`, then decimal digits, then `\n`
  (0x0800D7D8), and is served by the switch in **0x0800522C**, which the decompiler truncated to
  22 bytes. It really spans 0x0800522C..0x080054AA and replies `'Z'` via `uart3_putc`.
- The jump table sits after `bl __ARM_common_switch8` at 0x08005236. Count byte 0x0800523A = 12;
  the entry bytes give these case targets:

  | Case | a | b | c | d | e | f | g | h | i | j | k | l | default |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | Target | 0x08005248 | 0x080052A6 | 0x08005324 | 0x08005352 | 0x0800538A | 0x080053AC | 0x080053CE | 0x080053EE | 0x08005418 | 0x08005438 | 0x08005412 | 0x08005410 | 0x0800540E |

  0x08005410, 0x08005412 and 0x0800540E are branch stubs to 0x08005478 (l), 0x08005458 (k) and
  0x08005498 (default).
- `FUN_08012524` (wait for "ok") is mis-decompiled (section 2.1). ORE is never cleared (section 4.2).

### 7.3 Decompiler coverage gaps
`__ARM_common_switch8` (0x0800D9C8) jump tables are not followed. Code after these call sites is
missing or garbled in the decompiled output: 0x08005236 (USART3 dispatcher), 0x0800576C, 0x08005812,
0x080058BA (inside 0x08005760, whose real body extends to about 0x0800596C), and 0x08006F52 (in
the 0x08006EB4..0x0800773A gap after 0x080065D8). Table format: `byte count; byte offset[count+1]`;
target = (LR | 1) + 2·offset, where LR = address after the `bl`; index ≥ count uses the last
(default) entry.

### 7.4 For the application-logic spec
The TIM3 counters (4.3), the key codes (3.1), the licence flag 0x20000010 bit10 (5), the settings
block and program-slot layouts (6.1), and the `:w00` / `:w95` / `:w96` hardware side effects (5, 1.4).

---

## 8. Open questions
1. **PB0, PB1, PB2, PB7 roles.** PB0 is always low with a pull-down (an unused enable?). PB1 is an
   input that gates display control and service commands. PB2 is high while the display is in
   control, low after PB1 goes high, set and cleared by `:w92`/`:w91`, and toggled at about
   37..50 kHz after power-off. PB7 is a 16-level PWM driven by USART3 'b'. The schematic or a
   probe is needed.
2. PB6 and PB10 are configured as pulled-up inputs but never read (unpopulated keys?).
3. Crystal frequency: the code assumes 8 MHz (HSE_VALUE). If the board differs, all clocks
   and baud rates scale.
4. The real duration of `delay_loop` iterations (3 vs 4 vs more cycles). Measure one known
   delay, e.g. the LCD reset pulse on PC7.
5. Who writes 0x08003C00 (LCD variant) and 0x08003D00, and whether the bootloader erases or
   rewrites page 7 on update. What 0x08003F00 means to the bootloader.
6. SPI flash part number and size (no JEDEC ID is read).
7. The bootloader hand-off state: the app assumes PRIMASK=0 and USARTs/SPIs/TIM3 disabled
   (LL init silently skips enabled peripherals).
8. Reimplementation policy (decided): do NOT touch option bytes (no forced RDP; the unit is
   already at level 1, and lowering RDP mass-erases the vendor bootloader), do NOT reimplement the
   UID binding, never write the bootloader region 0x08000000..0x08003FFF. Fix the ORE lock-up
   (clear overrun) rather than reproducing it.
9. Which boards USART1/2/3 connect to (protocols spec). The ':w11'/':w15' traffic suggests
   USART1 and USART2 go to the two generator channels.

---

## 9. Measured on the user's unit (bring-up firmware, 2026-09-23)

| Item | Result | Consequence |
|---|---|---|
| LCD panel variant (u16 @ 0x08003C00) | **0xFF00 → variant A** (INVON, gamma F0…) | This unit runs init sequence A; the emulator default (erased → B) differs |
| HSE | starts; PLL 64 MHz from HSE | 8 MHz crystal confirmed working (frequency itself not independently measured) |
| `delay_loop` cost | **4.00 cycles / iteration** (SysTick) | Every original delay is 4/3 of its apparent intent (10 ms constant → 13.3 ms) |
| SPI NOR JEDEC ID | **EF 40 15 = Winbond W25Q16** (2 MB) | Storage map (section 6.1) fits easily |
| SPI flash @ 0x5000 | 00 00 00 00 00 00 | Name table area on this unit starts with zeros |
| PB1 with USB connected to the PC | **1 (high)** | Supports "PB1 = PC/USB connected" → the original would be in PC mode |
| Keys | all 8 keys and both combos report the expected codes | Key map in section 3.1 confirmed |
| Flashing via the vendor bootloader | custom image boots from 0x08004000 | No app validation by the bootloader beyond what the image provides |
| Serial routing, PB2 = 0 (link probe) | PC channel **…903 ↔ USART1**, PC channel **…901 ↔ USART2**, both directions (channel names of the unit measured; they differ per unit and computer: …903 is the second channel) | The PC shares the generator A/B links with the display; the vendor bootloader listens on USART1 (the flasher finds it by its hardware ID) |
| Serial routing, PB2 = 1 | **no PC ↔ display traffic in either tested direction** | PB2 = 1 isolates the PC; the display owns the generator links (matches the original's PB2 = 1 around its own generator commands) |
| Generator reaction to `#…` lines | none observed on either channel with PB2 = 0 | lines not starting with ':' are ignored by the generator boards (as far as visible to the PC) |

Schematic notes (docs/hardware, low fidelity, display-board pins derived from this spec): unpopulated 5-pin
JST `GND 5V Rx Tx FSEL` = USART3 factory port; unpopulated 1×5 (SWD?); unpopulated 3-pin `U4Tx U4Rx GND`;
display ↔ main board via 2-, 4- and 6-pin cables (5 V, GND, USART1, USART2, PB1, PB2 inferred).
