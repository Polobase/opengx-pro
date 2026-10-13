# OpenGX Pro: firmware for the GX Pro display board (STM32G070, app at 0x08004000).
#
#   make [APP=fw]        build build/<APP>/GxCustomFW.bin (flashable, obfuscated like the original);
#                        APP selects the entry main_<APP>.c and its module list below
#   make flash           flash it with tools/flash_gx_pro.py
#   make flash-original  put the original firmware back
#   make release         the nx image as it is published (build/release/); make flash-release flashes it
#
# Sources: src/ holds what both firmwares share (hardware layer, protocols, storage, wire-level
# cores), src/classic/ the classic firmware and its test and spike apps, src/nx/ the nx firmware
# (one flat folder).
#
# The apps in VENDOR_ASSET_APPS need the user's copy of the original image
# (original/GxDisplayFW.bin) for the fonts and bitmaps; they are extracted at build time and
# never committed.
#
# `make clean` deletes all of build/, including recorded device results (build/conf-*.json,
# build/latency-*.json). To rebuild one app from scratch use `rm -rf build/<APP>`.

APP      ?= fw
TARGET   := gxpro
BUILD    := build/$(APP)
ORIGINAL := original/GxDisplayFW.bin
PYTHON   := python3                  # build steps use the standard library only
FLASHER  := .venv/bin/python tools/flash_gx_pro.py

# Personal settings (never committed): e.g. TERRAIN_DIR and PRESET_FILE, where your Spooky2
# installation keeps its presets, for `make nx-data` (below). Variables only.
-include local.mk

CROSS   := arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

comma := ,

# ---- modules: every app links exactly the files it names ----

# Hardware layer: the part every app uses, and the classic apps' base, which adds the classic UART
# driver and key scan (src/classic/) in the original link order, so APP=fw stays byte-identical.
SHARED_BASE := src/board.c src/lcd.c src/spiflash.c src/fmt.c src/startup.c
BASE_SRCS   := src/board.c src/classic/uart.c src/lcd.c src/classic/keys.c src/spiflash.c src/fmt.c src/startup.c
# Classic hardware helpers: TIM3 10 kHz tick + soft PWM, polled LCD drawing, vendor-font text
CLASSIC_HW  := src/classic/board_tim3.c src/classic/lcd_draw.c src/classic/lcd_text.c
# Classic UI: the original screens, pixel for pixel (vendor fonts and bitmaps)
CLASSIC_UI  := src/classic/gfx.c src/classic/ui.c src/classic/ui_state.c
# Serial protocols and SPI-flash storage
PROTO_SRCS  := src/link.c src/storage.c src/factory.c src/calib.c
# Classic application: state machine, keys, program runner
CLASSIC_APP := src/classic/app.c src/classic/runner.c
# Wire-level cores shared with nx: exact generator lines and PC-visible sequences
CORE_SRCS   := src/runner_seq.c src/core_pcmode.c src/core_calmenu.c

MODULES_bringup  := $(CLASSIC_HW)
MODULES_probe    := $(CLASSIC_HW)
MODULES_uitest   := $(CLASSIC_HW) src/state.c $(CLASSIC_UI)
MODULES_linktest := $(CLASSIC_HW) src/state.c $(PROTO_SRCS)
MODULES_fw       := $(CLASSIC_HW) src/state.c $(CLASSIC_UI) $(PROTO_SRCS) $(CORE_SRCS) $(CLASSIC_APP)

# nx: next-generation firmware (docs/nx/fw2-design.md); open assets only. src/nx/nx_uart.c
# replaces src/classic/uart.c (same uart.h API) and src/nx/nx_keys.c the classic key scan; the
# hooks of the shared modules are defined here. The UI
# asset tables src/nx/nx_assets.{c,h} come from tools/gen_nx_assets.py (committed; `make nx-assets`).
# every src/nx/*.c except the entry files and the engine gallery (nxg_*.c: APP=nxgallery)
NX_SRCS     := $(filter-out src/nx/main_%.c src/nx/nxg_%.c,$(sort $(wildcard src/nx/*.c)))
BASE_SRCS_nx := $(SHARED_BASE)
MODULES_nx  := src/state.c $(PROTO_SRCS) $(CORE_SRCS) $(NX_SRCS)
# NX_ENG_OFF (src/nx/nx_config.mk): the UI engine features the image never uses; the engine
# gallery keeps them.
include src/nx/nx_config.mk
CFLAGS_nx   := -DAPP_NX=1 -DCFG_IWDG=1 -DCFG_FAULT_HOOK=nx_fault -DLINK_RX_TAP=nx_rx_tap \
               -DLINK_OK_HOOK=nx_txq_on_ok -DSPIFLASH_ACQUIRE=nx_flash_acquire -DSPIFLASH_POLL_HOOK=nx_flash_poll \
               $(NX_ENG_OFF) $(NX_LCD_OPTS)
# The original's Spooky2 ghost splash (tools/gen_nx_vendor.py): off by default since 2026-10-05
# (2.3 KB, docs/nx/drop1-size-inventory.md); make APP=nx NX_VENDOR=1 builds it in from the user's
# own original image, generated into $(BUILD) and never committed. Without it (and in the gallery
# and the host builds) the drawn splash shows.
NX_VENDOR   ?= 0
ifeq ($(APP)$(NX_VENDOR),nx1)
CFLAGS_nx   += -DNX_VENDOR_SPLASH=1
VENDOR_OBJ  := $(BUILD)/nx_vendor.o
endif
# The built-in Terrain Protocol and the built-in preset with sweeps: data tables taken from
# Spooky2's presets (tools/gen_nx_terrain.py, tools/gen_nx_preset.py; docs/nx/THIRD_PARTY.md),
# committed in src/nx/data/ so that every build has the same ones. NX_TERRAIN=0 builds without the
# Terrain rows, NX_PRESETS=0 without the preset row. `make nx-data` writes the tables again from a
# Spooky2 installation: TERRAIN_DIR names its folder "Preset Collections/Detox/Terrain Protocol",
# PRESET_FILE one preset .txt (local.mk or the command line).
TERRAIN_DIR ?=
PRESET_FILE ?=
NX_TERRAIN  ?= 1
NX_PRESETS  ?= 1
ifeq ($(APP)$(NX_TERRAIN),nx1)
CFLAGS_nx   += -DNX_TERRAIN_DATA=1
TERRAIN_OBJ := $(BUILD)/nx/data/nx_terrain_data.o
endif
ifeq ($(APP)$(NX_PRESETS),nx1)
CFLAGS_nx   += -DNX_PRESET_DATA=1
PRESET_OBJ  := $(BUILD)/nx/data/nx_preset_data.o
endif
# A device-test build whose program time runs NX_TIME_X times faster (dwells, Terrain hours; not the
# manual sweeps): make APP=nx NX_TIME_X=60 BUILD=build/nx-x60. Never for normal use.
ifdef NX_TIME_X
CFLAGS_nx   += -DNX_DWELL_UNITS=$(shell echo $$((1000 / $(NX_TIME_X))))u
endif
# The image that is published (`make release`, below): no artwork from the original image, the
# Terrain Protocol and the preset from the committed tables, and no test timing or LCD overrides.
# src/nx/nx_config.h and tools/release_check.py check the same again.
ifeq ($(APP)$(NX_RELEASE),nx1)
ifneq ($(NX_VENDOR)$(NX_TERRAIN)$(NX_PRESETS)$(NX_TIME_X)$(NX_LCD_OPTS),011)
$(error release build: NX_VENDOR must be 0, NX_TERRAIN and NX_PRESETS 1, NX_TIME_X and NX_LCD_OPTS empty)
endif
CFLAGS_nx   += -DNX_RELEASE=1
endif
LDSCRIPT_nx := ld/nx.ld
LDFLAGS_nx  := $(if $(NX_FLASH_LIMIT),-Wl$(comma)--defsym$(comma)NX_FLASH_LIMIT=$(NX_FLASH_LIMIT)) \
               $(if $(NX_NOINIT_ADDR),-Wl$(comma)--defsym$(comma)NX_NOINIT_ADDR=$(NX_NOINIT_ADDR))

# nx engine gallery (tools/nx_gallery.py run --emu): the UI engine and its sheets on a virtual
# clock, no firmware around them. NX_LCD_OPTS overrides Drop-0 constants of nx_config.h for a
# fallback build, e.g. make APP=nxgallery NX_LCD_OPTS=-DNX_LCD_DS16=0 BUILD=build/nxgallery-ds8
NX_ENGINE_SRCS := src/nx/nx_lcd.c src/nx/nx_lcd_port.c src/nx/nx_gfx.c src/nx/nx_render.c src/nx/nx_anim.c \
                  src/nx/nx_trans.c src/nx/nx_hero.c src/nx/nx_scope.c src/nx/nx_font.c src/nx/nx_bl.c \
                  src/nx/nx_assets.c
# the UI framework and its screens on the gallery's fixture core (src/nx/nxg_core.c)
NX_UI_SRCS := src/nx/nx_ui.c src/nx/nx_comp.c src/nx/nx_uiset.c src/nx/nx_uifmt.c src/nx/nx_keys.c \
              $(sort $(wildcard src/nx/nx_scr_*.c))
MODULES_nxgallery := $(NX_ENGINE_SRCS) $(NX_UI_SRCS) src/nx/nxg_sheets.c src/nx/nxg_stub.c src/nx/nxg_core.c \
                     src/nx/nxg_screens.c
CFLAGS_nxgallery  := -DAPP_NX=1 $(NX_LCD_OPTS)

# Drop-0 device spikes (docs/nx/drop0-checklist.md): local test apps built from the user's image
MODULES_sizeprobe := $(CLASSIC_HW)
MODULES_nxbring   := src/classic/lcd_draw.c src/classic/lcd_text.c

# Apps whose screens use the vendor fonts and bitmaps extracted from $(ORIGINAL)
VENDOR_ASSET_APPS := fw uitest bringup probe linktest sizeprobe nxbring

ifeq ($(origin MODULES_$(APP)),undefined)
$(error unknown APP=$(APP); the MODULES_* lists in the Makefile name the apps)
endif

# ---- flags; CFLAGS_<APP>, LDSCRIPT_<APP> and LDFLAGS_<APP> are optional per-app additions ----

CPU      := -mcpu=cortex-m0plus -mthumb -masm-syntax-unified
CFLAGS   := $(CPU) -std=c11 -Os -g3 -Wall -Wextra -Wshadow -ffunction-sections -fdata-sections -fno-common \
            -DSTM32G070xx -Isrc -I$(BUILD) -Ivendor/cmsis_core -Ivendor/cmsis_device_g0 $(CFLAGS_$(APP))
LDSCRIPT := $(or $(LDSCRIPT_$(APP)),ld/app.ld)
LDFLAGS  := $(CPU) -T $(LDSCRIPT) -nostartfiles -Wl,--gc-sections -Wl,--print-memory-usage \
            -Wl,-Map=$(BUILD)/$(TARGET).map --specs=nano.specs --specs=nosys.specs $(LDFLAGS_$(APP))

# The entry file main_<APP>.c lives in src/classic/ or src/nx/
MAIN := $(firstword $(wildcard src/classic/main_$(APP).c src/nx/main_$(APP).c))
SRCS := $(or $(BASE_SRCS_$(APP)),$(BASE_SRCS)) $(MODULES_$(APP)) $(MAIN)
ifneq ($(filter $(APP),$(VENDOR_ASSET_APPS)),)
ASSET_HDR := $(BUILD)/assets.h
ASSET_OBJ := $(BUILD)/assets.o
endif
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS)) $(ASSET_OBJ) $(VENDOR_OBJ) $(TERRAIN_OBJ) $(PRESET_OBJ)

all: $(BUILD)/GxCustomFW.bin

$(BUILD):
	mkdir -p $@

$(BUILD)/assets.c $(BUILD)/assets.h &: tools/gen_assets.py $(ORIGINAL) | $(BUILD)
	$(PYTHON) tools/gen_assets.py $(ORIGINAL) $(BUILD)

$(BUILD)/%.o: src/%.c $(ASSET_HDR) $(BUILD)/cflags.stamp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/assets.o: $(BUILD)/assets.c $(BUILD)/cflags.stamp
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/nx_vendor.c: tools/gen_nx_vendor.py tools/gen_assets.py $(ORIGINAL) | $(BUILD)
	$(PYTHON) tools/gen_nx_vendor.py $(ORIGINAL) $(BUILD)

$(BUILD)/nx_vendor.o: $(BUILD)/nx_vendor.c src/nx/nx_vendor.h $(BUILD)/cflags.stamp
	$(CC) $(CFLAGS) -c $< -o $@

# Stamp files that change only when the flags change (CFLAGS_<APP>; LDFLAGS e.g. NX_FLASH_LIMIT), so
# the objects are recompiled and the elf re-linked (re-running the size assert) although no
# source changed.
$(BUILD)/cflags.stamp: FORCE | $(BUILD)
	@echo '$(CC) $(CFLAGS)' | cmp -s - $@ || echo '$(CC) $(CFLAGS)' > $@

$(BUILD)/ldflags.stamp: FORCE | $(BUILD)
	@echo '$(LDFLAGS)' | cmp -s - $@ || echo '$(LDFLAGS)' > $@

FORCE:

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) $(BUILD)/ldflags.stamp
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/GxCustomFW.bin: $(BUILD)/$(TARGET).bin
	$(PYTHON) tools/gxfw.py encode $< $@ --id X2Vx

# The largest image the vendor bootloader is known to accept (docs/nx/drop0-checklist.md §1): `make
# flash` refuses anything bigger, whatever NX_FLASH_LIMIT the image was linked with (a raised one,
# up to the application region's 114672 bytes, is for the host and the emulator only). The size
# spike's own padded images (flash-padded) are exempt. 112640: the 110 KB size probe (55 blocks of
# 2 KB; the update file pads the image to 16 bytes and adds the 4 ID bytes, which the updater
# counts as a 56th block and never sends) arrived byte for byte (CRC OK) and ran on the unit on
# 2026-09-25, as did 96 and 104 KB.
FLASH_PROVEN ?= 112640

flash: $(BUILD)/GxCustomFW.bin
	@n=$$(wc -c < $(BUILD)/$(TARGET).bin | tr -d " "); test $$n -le $(FLASH_PROVEN) || \
	  { echo "refusing to flash $(BUILD): $$n bytes, more than FLASH_PROVEN=$(FLASH_PROVEN) (docs/nx/drop0-checklist.md §1)"; exit 2; }
	$(FLASHER) -f $<

flash-original:
	$(FLASHER) -f $(ORIGINAL)

# Image-size spike: APP=sizeprobe padded to PAD_KB kilobytes (tools/pad_image.py)
PAD_KB ?= 80
PADDED := $(BUILD)/GxSizeProbe_$(PAD_KB)k.bin
padded: $(PADDED)

$(PADDED): $(BUILD)/$(TARGET).bin tools/pad_image.py tools/gxfw.py
	@test "$(APP)" = sizeprobe || { echo "padded images are for APP=sizeprobe"; exit 2; }
	$(PYTHON) tools/pad_image.py $< $(BUILD)/padded_$(PAD_KB)k.bin --kb $(PAD_KB)
	$(PYTHON) tools/gxfw.py encode $(BUILD)/padded_$(PAD_KB)k.bin $@ --id X2Vx

flash-padded: $(PADDED)
	$(FLASHER) -f $<

# The image that is published: APP=nx from scratch in build/release, under the guard above, checked
# and named by tools/release_check.py:
# build/release/opengx-pro-<version>.bin (the update file), its .sha256 and manifest.json.
# `make release STRICT=1` (the release workflow) also refuses uncommitted changes and a tag that
# is not v<version>.
RELEASE_DIR  := build/release
RELEASE_VARS := APP=nx BUILD=$(RELEASE_DIR) NX_RELEASE=1 NX_VENDOR=0 NX_TERRAIN=1 NX_PRESETS=1 \
                NX_TIME_X= NX_LCD_OPTS= NX_FLASH_LIMIT= NX_NOINIT_ADDR=
release:
	rm -rf $(RELEASE_DIR)
	$(MAKE) $(RELEASE_VARS) all nx-size
	$(PYTHON) tools/release_check.py $(RELEASE_DIR) --limit $(FLASH_PROVEN) $(if $(STRICT),--strict)

# Flash the image `make release` built (device in programming mode first).
flash-release:
	@test -f $(RELEASE_DIR)/manifest.json || { echo "no release build: run \`make release\` first"; exit 2; }
	$(FLASHER) -f $(RELEASE_DIR)/GxCustomFW.bin

# Authenticated real-device conformance test, driven from here but run by the freqgen `spooky2`
# tool, which holds the vendor session handshake (not reimplemented in this repo). CONFORMANCE_DIR
# is its package folder in a checkout of github.com/Polobase/freqgen next to this repository. Usage:
#   make conformance PORT=/dev/cu.wchusbserial...903 RECORD=build/conf-original.json
#   make conformance PORT=... COMPARE=build/conf-original.json RECORD=build/conf-ours.json
CONFORMANCE_DIR ?= ../freqgen/packages/spooky2
PORT ?=
RECORD ?= build/conformance.json
conformance:
	@test -n "$(PORT)" || { echo "set PORT=/dev/cu.wchusbserial...903 (run: ls /dev/cu.wchusbserial*)"; exit 2; }
	@test -f "$(CONFORMANCE_DIR)/dist/cli.js" || { echo "build the freqgen tool first: (cd $(CONFORMANCE_DIR) && npm run build)"; exit 2; }
	@mkdir -p build
	node "$(CONFORMANCE_DIR)/dist/cli.js" conformance --device genx-pro --port "$(PORT)" \
	  $(if $(COMPARE),--compare "$(COMPARE)") --record "$(RECORD)"

# The Terrain and preset tables again from a Spooky2 installation (standard library only):
#   make nx-data TERRAIN_DIR=".../Preset Collections/Detox/Terrain Protocol" PRESET_FILE=".../a preset.txt"
nx-data:
	@test -d "$(TERRAIN_DIR)" || { echo "set TERRAIN_DIR to the folder Preset Collections/Detox/Terrain Protocol of a Spooky2 installation"; exit 2; }
	@test -f "$(PRESET_FILE)" || { echo "set PRESET_FILE to a preset .txt of a Spooky2 installation"; exit 2; }
	$(PYTHON) tools/gen_nx_terrain.py "$(TERRAIN_DIR)" src/nx/data/nx_terrain_data.c
	$(PYTHON) tools/gen_nx_preset.py "$(PRESET_FILE)" src/nx/data/nx_preset_data.c

# nx UI assets (docs/nx/fw2-design.md "UI / UX"). The generated tables src/nx/nx_assets.{c,h} and src/nx/nx_str_ids.h are
# committed, so building the firmware needs no Python packages. These targets use the .venv
# (Pillow with FreeType and raqm, tools/requirements.txt); nx-assets-fetch needs the network once.
NX_ASSET_TOOL := .venv/bin/python tools/gen_nx_assets.py
nx-assets-fetch:
	$(NX_ASSET_TOOL) fetch
nx-assets-test: nx-assets-fetch
	.venv/bin/python -m unittest discover -s tools/nxassets -t tools
nx-assets: nx-assets-fetch
	$(NX_ASSET_TOOL) build
nx-assets-check: nx-assets-fetch
	$(NX_ASSET_TOOL) check
nx-assets-preview: nx-assets-fetch
	$(NX_ASSET_TOOL) preview

# Flash and RAM of an app per module group, the image against NX_FLASH_LIMIT (tools/nx_size.py)
nx-size: $(BUILD)/$(TARGET).elf
	$(PYTHON) tools/nx_size.py $(BUILD)/$(TARGET).map --limit $(or $(NX_FLASH_LIMIT),112640)

.PHONY: nx-size

clean:
	rm -rf build

.PHONY: all flash flash-original padded flash-padded release flash-release nx-data conformance clean FORCE nx-assets-fetch nx-assets-test nx-assets nx-assets-check nx-assets-preview
-include $(OBJS:.o=.d)
