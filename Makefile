# Minimal cc65 C64 Makefile
# Override CC65_HOME if you need to point at a custom install.
# Example: make CC65_HOME=/opt/cc65

CC65_HOME ?=
CL65 := $(if $(CC65_HOME),$(CC65_HOME)/bin/cl65,cl65)
LD65 := $(if $(CC65_HOME),$(CC65_HOME)/bin/ld65,ld65)

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
ROOM_SOURCES := $(wildcard rooms/[0-9A-F][0-9A-F].c)
ROOM_IDS := $(notdir $(ROOM_ASSETS))
ROOM_OUTDIR := $(OUTDIR)/rooms
ROOM_CODES := $(addprefix $(ROOM_OUTDIR)/C,$(ROOM_IDS))
ROOM_CFG := cfg/room_overlay.cfg
DISK_EXTRA_FILES ?= $(wildcard $(RES_DIR)/*) $(ROOM_ASSETS) assets/objects.cobj $(ROOM_CODES)
DISK_EXTRA_DEPS = $(DISK_EXTRA_FILES)
ASSET_EDITOR_HOST ?= 127.0.0.1
ASSET_EDITOR_PORT ?= 8000

CFLAGS := -t $(TARGET) -Oirs --cpu 6502
LDFLAGS := -C $(CFG)

SOURCES_C := $(wildcard src/*.c)
SOURCES_S := $(filter-out src/text.s,$(wildcard src/*.s))
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
TEXT_MODULE_OBJ := $(OUTDIR)/text-module.o
TEXT_HEADER_OBJ := $(OUTDIR)/text-header.o
TEXT_RESOLVER_SRC := $(OUTDIR)/text-resolver.s
TEXT_RESOLVER_OBJ := $(OUTDIR)/text-resolver.o
TEXT_MODULE_CFG := cfg/text_module.cfg
TEXT_MODULE_PRG := $(OUTDIR)/text.prg
DISK_BOOT_OBJ := $(OUTDIR)/disk-boot.o
DISK_BOOT_CFG := cfg/disk_boot.cfg
DISK_BOOT_PRG := $(OUTDIR)/disk-boot.prg

.PHONY: all clean d64 cartridge run run-d64 run-cartridge asset-editor

all: $(OUT_PRG) $(TEXT_MODULE_PRG)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(ROOM_OUTDIR):
	mkdir -p $(ROOM_OUTDIR)

$(OUTDIR)/%.o: src/%.c | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/%.o: src/%.s $(wildcard src/*.inc) | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/assets.o: $(ASSETS)

$(OUT_PRG): $(OBJECTS) $(CFG) tools/validate_prg_layout.py
	$(CL65) $(CFLAGS) $(LDFLAGS) -m $(OUT_MAP) -Ln $(OUT_LBL) -o $@ $(OBJECTS)
	python3 tools/validate_prg_layout.py --prg $@ --map $(OUT_MAP)

$(TEXT_MODULE_OBJ): src/text.s src/platform.inc | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_HEADER_OBJ): modules/text_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_RESOLVER_SRC): $(OUT_PRG) $(TEXT_MODULE_OBJ) \
		tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(TEXT_MODULE_OBJ)

$(TEXT_RESOLVER_OBJ): $(TEXT_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_MODULE_PRG): $(TEXT_HEADER_OBJ) $(TEXT_MODULE_OBJ) \
		$(TEXT_RESOLVER_OBJ) $(TEXT_MODULE_CFG)
	$(LD65) -C $(TEXT_MODULE_CFG) -m $(OUTDIR)/text.map -o $@ \
		$(TEXT_HEADER_OBJ) $(TEXT_MODULE_OBJ) $(TEXT_RESOLVER_OBJ)

$(DISK_BOOT_OBJ): disk/boot.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(DISK_BOOT_PRG): $(DISK_BOOT_OBJ) $(DISK_BOOT_CFG)
	$(LD65) -C $(DISK_BOOT_CFG) -m $(OUTDIR)/disk-boot.map -o $@ $<

$(ROOM_OUTDIR)/room-%.o: rooms/%.c src/game.h src/platform.h | $(ROOM_OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(ROOM_OUTDIR)/header-%.o: rooms/room_header.s | $(ROOM_OUTDIR)
	$(CL65) $(CFLAGS) --asm-define ROOM_ID=0x$* -c -o $@ $<

$(ROOM_OUTDIR)/resolver-%.s: $(OUT_PRG) $(ROOM_OUTDIR)/room-%.o \
		$(ROOM_OUTDIR)/header-%.o \
		tools/generate_room_resolver.py | $(ROOM_OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(ROOM_OUTDIR)/room-$*.o $(ROOM_OUTDIR)/header-$*.o

$(ROOM_OUTDIR)/resolver-%.o: $(ROOM_OUTDIR)/resolver-%.s
	$(CL65) $(CFLAGS) -c -o $@ $<

$(ROOM_OUTDIR)/room-%.raw: $(ROOM_OUTDIR)/header-%.o $(ROOM_OUTDIR)/room-%.o \
		$(ROOM_OUTDIR)/resolver-%.o $(ROOM_CFG)
	$(LD65) -C $(ROOM_CFG) -m $(ROOM_OUTDIR)/room-$*.map -o $@ \
		$(ROOM_OUTDIR)/header-$*.o $(ROOM_OUTDIR)/room-$*.o \
		$(ROOM_OUTDIR)/resolver-$*.o

$(ROOM_OUTDIR)/C%: $(ROOM_OUTDIR)/room-%.raw tools/finalize_room_code.py
	python3 tools/finalize_room_code.py --input $< \
		--map $(ROOM_OUTDIR)/room-$*.map --room $* --output $@

$(EF_BOOT_OBJ): cart/ef_boot.s $(OUT_PRG) $(TEXT_MODULE_PRG) | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(OUT_EF_BASE): $(EF_BOOT_OBJ) $(EF_CFG)
	$(CL65) -t $(TARGET) --cpu 6502 -C $(EF_CFG) -m $(OUTDIR)/game-ef.map -o $@ $(EF_BOOT_OBJ)

$(OUT_EF_BIN): $(OUT_EF_BASE) $(TEXT_MODULE_PRG) tools/pack_easyflash.py \
		$(ROOM_ASSETS) assets/objects.cobj $(ROOM_CODES)
	python3 tools/pack_easyflash.py --base $(OUT_EF_BASE) --assets assets \
		--objects assets/objects.cobj --room-code $(ROOM_OUTDIR) --output $@

$(OUT_CRT): $(OUT_EF_BIN)
	$(CARTCONV) -p -t easy -i $< -o $@ -n "$(CART_NAME)"

cartridge: $(OUT_CRT)

d64: $(OUT_D64)

$(OUT_D64): $(OUT_PRG) $(TEXT_MODULE_PRG) $(DISK_BOOT_PRG) \
		$(DISK_EXTRA_DEPS) | $(OUTDIR)
	rm -f $@
	$(C1541) -format "$(DISK_NAME),00" d64 $@
	$(C1541) $@ -write $(DISK_BOOT_PRG) "$(PRG_NAME)"
	$(C1541) $@ -write $(OUT_PRG) "ENGINE"
	$(C1541) $@ -write $(TEXT_MODULE_PRG) "TEXT"
	for f in $(DISK_EXTRA_FILES); do \
		[ -f "$$f" ] || continue; \
		name=$$(basename "$$f"); \
		$(C1541) $@ -write "$$f" "$$name"; \
	done

run: $(OUT_D64)
	$(VICE) -autostart $(OUT_D64)

run-d64: $(OUT_D64)
	$(VICE) -autostart $(OUT_D64)

run-cartridge: $(OUT_CRT)
	$(VICE) -cartcrt $(OUT_CRT)

asset-editor:
	python3 tools/asset-editor/server.py --host $(ASSET_EDITOR_HOST) --port $(ASSET_EDITOR_PORT)

clean:
	rm -rf $(OUTDIR)
