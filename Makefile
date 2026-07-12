# Minimal cc65 C64 Makefile
# Override CC65_HOME if you need to point at a custom install.
# Example: make CC65_HOME=/opt/cc65

CC65_HOME ?=
CL65 := $(if $(CC65_HOME),$(CC65_HOME)/bin/cl65,cl65)

TARGET := c64
OUTDIR := build
CFG    := cfg/myc64.cfg
VICE   ?= x64sc
C1541  ?= c1541
CARTCONV ?= cartconv
DISK_NAME ?= GAME
PRG_NAME  ?= GAME
CART_NAME ?= GAME
RES_DIR ?= res
ROOM_ASSETS := $(wildcard assets/[0-9A-F][0-9A-F])
DISK_EXTRA_FILES ?= $(wildcard $(RES_DIR)/*) $(ROOM_ASSETS) assets/objects.cobj
DISK_EXTRA_DEPS := $(wildcard $(DISK_EXTRA_FILES))
ASSET_EDITOR_HOST ?= 127.0.0.1
ASSET_EDITOR_PORT ?= 8000

CFLAGS := -t $(TARGET) -Oirs --cpu 6502
LDFLAGS := -C $(CFG)

SOURCES_C := $(wildcard src/*.c)
SOURCES_S := $(wildcard src/*.s)
ASSETS := assets/charset.cchr assets/tiles.ctil assets/00 assets/objects.cobj
OBJECTS := $(patsubst src/%.c,$(OUTDIR)/%.o,$(SOURCES_C)) \
           $(patsubst src/%.s,$(OUTDIR)/%.o,$(SOURCES_S))

OUT_PRG := $(OUTDIR)/game.prg
OUT_MAP := $(OUTDIR)/game.map
OUT_LBL := $(OUTDIR)/game.lbl
OUT_D64 := $(OUTDIR)/game.d64
OUT_EF_BIN := $(OUTDIR)/game-ef.bin
OUT_EF_BASE := $(OUTDIR)/game-ef-base.bin
OUT_CRT := $(OUTDIR)/game.crt
EF_BOOT_OBJ := $(OUTDIR)/ef_boot.o
EF_CFG := cfg/easyflash.cfg

.PHONY: all clean d64 cartridge run run-d64 run-cartridge asset-editor

all: $(OUT_PRG)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/%.o: src/%.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/assets.o: $(ASSETS)

$(OUT_PRG): $(OBJECTS) tools/validate_prg_layout.py
	$(CL65) $(CFLAGS) $(LDFLAGS) -m $(OUT_MAP) -Ln $(OUT_LBL) -o $@ $(OBJECTS)
	python3 tools/validate_prg_layout.py --prg $@ --map $(OUT_MAP)

$(EF_BOOT_OBJ): cart/ef_boot.s $(OUT_PRG) | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUT_EF_BASE): $(EF_BOOT_OBJ) $(EF_CFG)
	$(CL65) -t $(TARGET) --cpu 6502 -C $(EF_CFG) -m $(OUTDIR)/game-ef.map -o $@ $(EF_BOOT_OBJ)

$(OUT_EF_BIN): $(OUT_EF_BASE) tools/pack_easyflash.py $(ROOM_ASSETS) assets/objects.cobj
	python3 tools/pack_easyflash.py --base $(OUT_EF_BASE) --assets assets \
		--objects assets/objects.cobj --output $@

$(OUT_CRT): $(OUT_EF_BIN)
	$(CARTCONV) -p -t easy -i $< -o $@ -n "$(CART_NAME)"

cartridge: $(OUT_CRT)

d64: $(OUT_D64)

$(OUT_D64): $(OUT_PRG) $(DISK_EXTRA_DEPS) | $(OUTDIR)
	rm -f $@
	$(C1541) -format "$(DISK_NAME),00" d64 $@
	$(C1541) $@ -write $(OUT_PRG) "$(PRG_NAME)"
	for f in $(DISK_EXTRA_FILES); do \
		[ -f "$$f" ] || continue; \
		name=$$(basename "$$f"); \
		$(C1541) $@ -write "$$f" "$$name"; \
	done

run: $(OUT_PRG)
	$(VICE) -autostart $(OUT_PRG)

run-d64: $(OUT_D64)
	$(VICE) -8 $(OUT_D64)

run-cartridge: $(OUT_CRT)
	$(VICE) -cartcrt $(OUT_CRT)

asset-editor:
	python3 tools/asset-editor/server.py --host $(ASSET_EDITOR_HOST) --port $(ASSET_EDITOR_PORT)

clean:
	rm -rf $(OUTDIR)
