# Host unit tests; `make -C tests/host` builds and runs the three binaries:
#   test_link       the link protocols, the SPI-flash storage and the shared cores (src/link.c,
#                   factory.c, calib.c, storage.c, spiflash.c, runner_seq.c, core_*.c; the classic
#                   runner src/classic/runner.c as the reference) with mocks of the UARTs and the
#                   board and a W25Q16 model; the newest SPI-flash backup of your own unit
#                   (backups/spi_flash_*.bin, read only) is checked when there is one
#   test_nx         the nx firmware's modules (tests/host/nx/): same mocks, the W25Q16 model with
#                   microsecond timing and power cuts, a simulated clock
#   test_nx_assets  the nx asset tables and their decoder (below)

CC     ?= cc
SRC    := ../../src
CLASSIC := $(SRC)/classic
BUILD  := build
BACKUP := $(abspath $(or $(lastword $(sort $(wildcard ../../backups/spi_flash_*.bin))),../../backups/spi_flash.bin))

# UBSan always; AddressSanitizer with `make ASAN=1` (its runtime hangs at start-up on some
# macOS versions).
comma  := ,
SAN    := undefined$(if $(ASAN),$(comma)address)
CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Wshadow -Werror -Wno-deprecated-declarations -DHOST_TEST \
          -fno-common -fsanitize=$(SAN) -fno-omit-frame-pointer -fno-sanitize-recover=all \
          -Imock -I. -I$(SRC) -I$(CLASSIC) -DLINK_RX_TAP=test_rx_tap -DLINK_OK_HOOK=test_ok_hook \
          -DSPIFLASH_ACQUIRE=test_flash_acquire -DSPIFLASH_POLL_HOOK=test_flash_poll

SRCS := test_link.c test_core.c mocks.c mock_classic.c w25q16.c simclock.c \
        $(SRC)/link.c $(SRC)/factory.c $(SRC)/calib.c $(SRC)/storage.c $(SRC)/spiflash.c $(SRC)/state.c \
        $(CLASSIC)/runner.c $(SRC)/runner_seq.c $(SRC)/fmt.c $(SRC)/core_pcmode.c $(SRC)/core_calmenu.c
HDRS := harness.h mock/mock_board.h $(wildcard $(SRC)/link*.h) $(SRC)/storage.h $(SRC)/spiflash.h \
        $(SRC)/state.h $(SRC)/uart.h $(SRC)/ui.h $(SRC)/lcd.h $(CLASSIC)/app.h $(SRC)/fmt.h $(SRC)/runner_seq.h \
        $(SRC)/core_pcmode.h $(SRC)/core_calmenu.h $(SRC)/keys.h

BIN    := $(BUILD)/test_link$(if $(ASAN),_asan)

# test_nx: its own main, the nx firmware's hook defines (as CFLAGS_nx in the top Makefile) and every
# nx module except the device drivers, the entry files and the engine gallery (nxg_*.c, own binaries);
# tests/host/nx/nx_world.c models the board around them. The classic runner is linked as the
# reference of the runner equivalence test. The Terrain and preset tables are the made-up ones of
# nx/terrain_fixture.c and nx/preset_fixture.c (NX_TERRAIN_DATA, NX_PRESET_DATA below), not the
# committed tables of src/nx/data/, which hold Spooky2's preset data.
NX_DRIVERS := $(SRC)/nx/nx_hw.c $(SRC)/nx/nx_uart.c $(SRC)/nx/nx_wdg.c $(SRC)/nx/nx_lcd_port.c \
              $(wildcard $(SRC)/nx/main_*.c) \
              $(wildcard $(SRC)/nx/nxg_*.c)
NX_CFLAGS := -std=c11 -g -O1 -Wall -Wextra -Wshadow -Werror -Wno-deprecated-declarations -DHOST_TEST \
             -fno-common -fsanitize=$(SAN) -fno-omit-frame-pointer -fno-sanitize-recover=all \
             -Imock -I. -Inx -I$(SRC) -I$(SRC)/nx -I$(CLASSIC) -DAPP_NX=1 -DLINK_RX_TAP=nx_rx_tap \
             -DLINK_OK_HOOK=nx_txq_on_ok -DSPIFLASH_ACQUIRE=nx_flash_acquire -DSPIFLASH_POLL_HOOK=nx_flash_poll \
             -DNX_TERRAIN_DATA=1 -DNX_PRESET_DATA=1 $(NX_OPTS)
# The unit tests keep the features on that the image leaves out by default (src/nx/nx_config.h), so
# their code stays tested; the gallery binaries below render what the image shows. NX_OPTS adds
# switches to all of them, e.g. make NX_OPTS=-DNX_FEAT_HERO48=1 (the 48 px digits' layout tests).
NX_TEST_ON := -DNX_FEAT_HIST=1 -DNX_FEAT_BFB=1
NX_SRCS   := $(filter-out nx/test_assets.c,$(wildcard nx/*.c)) mocks.c w25q16.c simclock.c \
             $(SRC)/link.c $(SRC)/factory.c $(SRC)/calib.c $(SRC)/storage.c $(SRC)/spiflash.c \
             $(SRC)/state.c $(SRC)/fmt.c $(CLASSIC)/runner.c $(SRC)/runner_seq.c $(SRC)/core_pcmode.c \
             $(SRC)/core_calmenu.c $(filter-out $(NX_DRIVERS),$(wildcard $(SRC)/nx/*.c))
NX_HDRS   := harness.h $(wildcard nx/*.h) mock/mock_board.h $(wildcard $(SRC)/*.h) $(wildcard $(SRC)/nx/*.h) \
             $(CLASSIC)/app.h
NX_BIN    := $(BUILD)/test_nx$(if $(ASAN),_asan)

test: $(BIN) $(NX_BIN)
	./$(BIN) "$(BACKUP)"
	./$(NX_BIN)

$(BIN): $(SRCS) $(HDRS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ -lm

$(NX_BIN): $(NX_SRCS) $(NX_HDRS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(NX_CFLAGS) $(NX_TEST_ON) $(NX_SRCS) -o $@ -lm

clean:
	rm -rf $(BUILD)

.PHONY: test clean

# nx asset tables and their decoder (src/nx/nx_font.c, src/nx/nx_assets.c): pure modules, own main,
# goldens from tools/gen_nx_assets.py (nx/nx_asset_vectors.h)
ASSET_SRCS := nx/test_assets.c $(SRC)/nx/nx_font.c $(SRC)/nx/nx_assets.c
ASSET_HDRS := harness.h nx/nx_asset_vectors.h $(SRC)/nx/nx_font.h $(SRC)/nx/nx_asset_fmt.h \
              $(SRC)/nx/nx_assets.h $(SRC)/nx/nx_str_ids.h
ASSET_BIN  := $(BUILD)/test_nx_assets$(if $(ASAN),_asan)

test: test-nx-assets

test-nx-assets: $(ASSET_BIN)
	./$(ASSET_BIN)

$(ASSET_BIN): $(ASSET_SRCS) $(ASSET_HDRS) Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(ASSET_SRCS) -o $@

.PHONY: test-nx-assets

# nx engine gallery on the host (tools/nx_gallery.py run --host): the sheets of src/nx/nxg_sheets.c
# through nx_lcd onto the panel model, one PPM per shot: `build/nxg_host DIR`
GALLERY_SRCS := gallery/nxg_host.c nx/nx_panel.c $(SRC)/nx/nxg_sheets.c $(SRC)/nx/nx_lcd.c $(SRC)/nx/nx_gfx.c \
                $(SRC)/nx/nx_render.c $(SRC)/nx/nx_anim.c $(SRC)/nx/nx_trans.c $(SRC)/nx/nx_hero.c \
                $(SRC)/nx/nx_scope.c $(SRC)/nx/nx_font.c $(SRC)/nx/nx_bl.c $(SRC)/nx/nx_assets.c \
                $(SRC)/nx/nx_ui.c $(SRC)/nx/nx_comp.c $(SRC)/nx/nx_uiset.c $(SRC)/nx/nx_uifmt.c $(SRC)/nx/nx_keys.c \
                $(wildcard $(SRC)/nx/nx_scr_*.c) $(SRC)/nx/nxg_core.c $(SRC)/nx/nxg_screens.c
GALLERY_BIN  := $(BUILD)/nxg_host

gallery: $(GALLERY_BIN)

$(GALLERY_BIN): $(GALLERY_SRCS) $(wildcard $(SRC)/nx/*.h) $(SRC)/nx/nxg_sheets.h nx/nx_panel.h Makefile
	@mkdir -p $(BUILD)
	$(CC) $(NX_CFLAGS) -O2 $(GALLERY_SRCS) -o $@

# The screens with the image's engine switches (NX_ENG_OFF, the list the top Makefile's CFLAGS_nx
# uses): only the screen fixtures (nxg_screens_run; the sheets need the features switched off), so
# the configuration the device runs is pixel-tested too (tools/nx_gallery.py run --host-dev):
# `build/nxg_host_dev DIR`
include $(SRC)/nx/nx_config.mk
GALLERY_DEV_SRCS := $(filter-out $(SRC)/nx/nxg_sheets.c,$(GALLERY_SRCS))
GALLERY_DEV_BIN  := $(BUILD)/nxg_host_dev

gallery-dev: $(GALLERY_DEV_BIN)

$(GALLERY_DEV_BIN): $(GALLERY_DEV_SRCS) $(wildcard $(SRC)/nx/*.h) nx/nx_panel.h $(SRC)/nx/nx_config.mk Makefile
	@mkdir -p $(BUILD)
	$(CC) $(NX_CFLAGS) -O2 $(NX_ENG_OFF) -DNXG_SCREENS_ONLY=1 $(GALLERY_DEV_SRCS) -o $@

.PHONY: gallery gallery-dev
