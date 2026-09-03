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
SAVE_DISK ?= $(OUTDIR)/saves.d64
SAVE_DISK_NAME ?= SAVES
RES_DIR ?= res
ROOM_ASSETS := $(wildcard assets/[0-9A-F][0-9A-F])
ROOM_SOURCES := $(wildcard rooms/[0-9A-F][0-9A-F].c)
ROOM_IDS := $(notdir $(ROOM_ASSETS))
ROOM_OUTDIR := $(OUTDIR)/rooms
ROOM_CODES := $(addprefix $(ROOM_OUTDIR)/C,$(ROOM_IDS))
C64_ASSET_OUTDIR := $(OUTDIR)/assets
C64_ROOM_ASSETS := $(addprefix $(C64_ASSET_OUTDIR)/,$(ROOM_IDS))
C64_OBJECT_TYPES := $(C64_ASSET_OUTDIR)/objects.cobj
C64_OBJECT_TYPES_INITIAL := $(C64_ASSET_OUTDIR)/objects-initial.hot
PORTRAIT_ASSETS := $(wildcard assets/portraits/[0-9A-F][0-9A-F])
PORTRAIT_IDS := $(notdir $(PORTRAIT_ASSETS))
C64_PORTRAIT_ASSETS := $(addprefix $(C64_ASSET_OUTDIR)/P,$(PORTRAIT_IDS))
# Each script/conversation/room declaration kind has its own independent
# 0-255 resource ID space (three separate directories - see
# src/platform.h's PLATFORM_RESOURCE_KIND_* and tools/pack_easyflash.py),
# so each gets its own source location and build-output prefix (RS/RC/RR).
# assets/resources/<ID> (raw, unstructured bytes) shares the script kind's
# ID space with compiled cutscenes, since neither belongs to a room or
# conversation.
RESOURCE_ASSETS := $(wildcard assets/resources/[0-9A-F][0-9A-F])
CUTSCENE_SOURCES := $(wildcard assets/scripts/cutscenes/[0-9A-F][0-9A-F].script)
CUTSCENE_IDS := $(basename $(notdir $(CUTSCENE_SOURCES)))
SCRIPT_IDS := $(sort $(notdir $(RESOURCE_ASSETS)) $(CUTSCENE_IDS))
C64_SCRIPT_ASSETS := $(addprefix $(C64_ASSET_OUTDIR)/RS,$(SCRIPT_IDS))
CONVERSATION_SOURCES := $(wildcard assets/scripts/conversations/[0-9A-F][0-9A-F].script)
CONVERSATION_IDS := $(basename $(notdir $(CONVERSATION_SOURCES)))
C64_CONVERSATION_ASSETS := $(addprefix $(C64_ASSET_OUTDIR)/RC,$(CONVERSATION_IDS))
ROOM_SCRIPT_SOURCES := $(wildcard assets/scripts/[0-9A-F][0-9A-F].script)
ROOM_SCRIPT_IDS := $(basename $(notdir $(ROOM_SCRIPT_SOURCES)))
C64_ROOM_SCRIPT_ASSETS := $(addprefix $(C64_ASSET_OUTDIR)/RR,$(ROOM_SCRIPT_IDS))
C64_RESOURCE_ASSETS := $(C64_SCRIPT_ASSETS) $(C64_CONVERSATION_ASSETS) $(C64_ROOM_SCRIPT_ASSETS)
ROOM_CFG := cfg/room_overlay.cfg
DISK_EXTRA_FILES ?= $(wildcard $(RES_DIR)/*) $(C64_ROOM_ASSETS) $(C64_OBJECT_TYPES) $(C64_PORTRAIT_ASSETS) $(ROOM_CODES) $(INVENTORY_MODULE)
DISK_EXTRA_DEPS = $(DISK_EXTRA_FILES)
ASSET_EDITOR_HOST ?= 127.0.0.1
ASSET_EDITOR_PORT ?= 8000

CFLAGS := -t $(TARGET) -Oirs --cpu 6502
LDFLAGS := -C $(CFG)

SOURCES_C := $(filter-out src/sid.c,$(wildcard src/*.c))
SOURCES_S := $(filter-out src/text.s,$(wildcard src/*.s))
ASSETS := assets/charset.cchr assets/tiles.ctil $(C64_ASSET_OUTDIR)/00 $(C64_OBJECT_TYPES) \
          $(C64_OBJECT_TYPES_INITIAL)
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
TEXT_SID_OBJ := $(OUTDIR)/text-sid.o
TEXT_VALIDATOR_OBJ := $(OUTDIR)/text-validator.o
TEXT_HEADER_OBJ := $(OUTDIR)/text-header.o
TEXT_RESOLVER_SRC := $(OUTDIR)/text-resolver.s
TEXT_RESOLVER_OBJ := $(OUTDIR)/text-resolver.o
TEXT_MODULE_CFG := cfg/text_module.cfg
TEXT_MODULE_PRG := $(OUTDIR)/text.prg
INVENTORY_MODULE_C_OBJ := $(OUTDIR)/inventory-module.o
INVENTORY_MODULE_ASM_OBJ := $(OUTDIR)/inventory-module-asm.o
INVENTORY_STORY_OBJ := $(OUTDIR)/inventory-story.o
INVENTORY_HEADER_OBJ := $(OUTDIR)/inventory-header.o
INVENTORY_RESOLVER_SRC := $(OUTDIR)/inventory-resolver.s
INVENTORY_RESOLVER_OBJ := $(OUTDIR)/inventory-resolver.o
INVENTORY_MODULE_CFG := cfg/inventory_overlay.cfg
INVENTORY_MODULE_RAW := $(OUTDIR)/inventory.raw
INVENTORY_MODULE := $(OUTDIR)/IV
SAVELOAD_MODULE_C_OBJ := $(OUTDIR)/saveload-module.o
SAVELOAD_DISK_OBJ := $(OUTDIR)/saveload-disk.o
SAVELOAD_HEADER_OBJ := $(OUTDIR)/saveload-header.o
SAVELOAD_RESOLVER_SRC := $(OUTDIR)/saveload-resolver.s
SAVELOAD_RESOLVER_OBJ := $(OUTDIR)/saveload-resolver.o
SAVELOAD_MODULE_CFG := cfg/saveload_overlay.cfg
SAVELOAD_MODULE_RAW := $(OUTDIR)/saveload.raw
SAVELOAD_MODULE := $(OUTDIR)/SL
SAVELOAD_SAVE_MODULE_C_OBJ := $(OUTDIR)/saveload-save-module.o
SAVELOAD_SAVE_HEADER_OBJ := $(OUTDIR)/saveload-save-header.o
SAVELOAD_SAVE_RESOLVER_SRC := $(OUTDIR)/saveload-save-resolver.s
SAVELOAD_SAVE_RESOLVER_OBJ := $(OUTDIR)/saveload-save-resolver.o
SAVELOAD_SAVE_MODULE_CFG := cfg/saveload_save_overlay.cfg
SAVELOAD_SAVE_MODULE_RAW := $(OUTDIR)/saveload-save.raw
SAVELOAD_SAVE_MODULE := $(OUTDIR)/SV
ROOM_HELPERS_C_OBJ := $(OUTDIR)/room-helpers-module.o
ROOM_HELPERS_HEADER_OBJ := $(OUTDIR)/room-helpers-header.o
ROOM_HELPERS_RESOLVER_SRC := $(OUTDIR)/room-helpers-resolver.s
ROOM_HELPERS_RESOLVER_OBJ := $(OUTDIR)/room-helpers-resolver.o
ROOM_HELPERS_MODULE_CFG := cfg/room_helpers_overlay.cfg
ROOM_HELPERS_MODULE_RAW := $(OUTDIR)/room-helpers.raw
ROOM_HELPERS_MODULE := $(OUTDIR)/RH
SCRIPT_C_OBJ := $(OUTDIR)/script-module.o
SCRIPT_HEADER_OBJ := $(OUTDIR)/script-header.o
SCRIPT_RESOLVER_SRC := $(OUTDIR)/script-resolver.s
SCRIPT_RESOLVER_OBJ := $(OUTDIR)/script-resolver.o
SCRIPT_MODULE_CFG := cfg/script_overlay.cfg
SCRIPT_MODULE_RAW := $(OUTDIR)/script.raw
SCRIPT_MODULE := $(OUTDIR)/SC
DISK_BOOT_OBJ := $(OUTDIR)/disk-boot.o
DISK_BOOT_CFG := cfg/disk_boot.cfg
DISK_BOOT_PRG := $(OUTDIR)/disk-boot.prg

.PHONY: all clean d64 cartridge run run-d64 run-cartridge asset-editor

all: $(OUT_PRG) $(TEXT_MODULE_PRG) $(INVENTORY_MODULE) $(SAVELOAD_MODULE) $(SAVELOAD_SAVE_MODULE) \
	$(ROOM_HELPERS_MODULE) $(SCRIPT_MODULE)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(ROOM_OUTDIR):
	mkdir -p $(ROOM_OUTDIR)

$(C64_ASSET_OUTDIR):
	mkdir -p $(C64_ASSET_OUTDIR)

$(C64_OBJECT_TYPES): assets/objects.cobj tools/prepare_c64_assets.py | $(C64_ASSET_OUTDIR)
	python3 tools/prepare_c64_assets.py objects $< $@

# Count must match INITIAL_OBJECT_TYPE_COUNT in src/platform.c.
$(C64_OBJECT_TYPES_INITIAL): $(C64_OBJECT_TYPES) tools/extract_initial_object_types.py
	python3 tools/extract_initial_object_types.py --input $< --count 2 --output $@

$(C64_ASSET_OUTDIR)/%: assets/% tools/prepare_c64_assets.py | $(C64_ASSET_OUTDIR)
	python3 tools/prepare_c64_assets.py room $< $@

$(C64_ASSET_OUTDIR)/P%: assets/portraits/% | $(C64_ASSET_OUTDIR)
	cp $< $@

$(C64_ASSET_OUTDIR)/RS%: assets/resources/% | $(C64_ASSET_OUTDIR)
	cp $< $@

# Script/conversation/room declarations are source (assets/scripts/...,
# one top-level declaration each - see tools/compile_script.py), compiled
# straight to the build output, the same way rooms/<ID>.c compiles straight
# to build/rooms/C<ID> without an intermediate assets/ artifact. --expect-
# kind catches a file in the wrong location (e.g. a `conversation`
# declaration under cutscenes/) as a build error instead of a silently
# misfiled resource.
$(C64_ASSET_OUTDIR)/RS%: assets/scripts/cutscenes/%.script tools/compile_script.py src/story.h | $(C64_ASSET_OUTDIR)
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind script

$(C64_ASSET_OUTDIR)/RC%: assets/scripts/conversations/%.script tools/compile_script.py src/story.h | $(C64_ASSET_OUTDIR)
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind conversation

$(C64_ASSET_OUTDIR)/RR%: assets/scripts/%.script tools/compile_script.py src/story.h | $(C64_ASSET_OUTDIR)
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind room

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

$(TEXT_SID_OBJ): src/sid.c src/sid.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_VALIDATOR_OBJ): modules/inventory_validate_post.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_HEADER_OBJ): modules/text_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_RESOLVER_SRC): $(OUT_PRG) $(TEXT_MODULE_OBJ) $(TEXT_SID_OBJ) \
		$(TEXT_VALIDATOR_OBJ) \
		tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(TEXT_MODULE_OBJ) $(TEXT_SID_OBJ) $(TEXT_VALIDATOR_OBJ)

$(TEXT_RESOLVER_OBJ): $(TEXT_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(TEXT_MODULE_PRG): $(TEXT_HEADER_OBJ) $(TEXT_MODULE_OBJ) $(TEXT_SID_OBJ) \
		$(TEXT_VALIDATOR_OBJ) \
		$(TEXT_RESOLVER_OBJ) $(TEXT_MODULE_CFG)
	$(LD65) -C $(TEXT_MODULE_CFG) -m $(OUTDIR)/text.map -o $@ \
		$(TEXT_HEADER_OBJ) $(TEXT_MODULE_OBJ) $(TEXT_SID_OBJ) \
		$(TEXT_VALIDATOR_OBJ) $(TEXT_RESOLVER_OBJ)

$(INVENTORY_MODULE_C_OBJ): modules/inventory.c src/game.h src/platform.h \
		src/story.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(INVENTORY_MODULE_ASM_OBJ): modules/inventory_draw.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(INVENTORY_STORY_OBJ): story/story.c src/game.h src/platform.h src/story.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(INVENTORY_HEADER_OBJ): modules/inventory_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(INVENTORY_RESOLVER_SRC): $(OUT_PRG) $(INVENTORY_MODULE_C_OBJ) \
		$(INVENTORY_MODULE_ASM_OBJ) $(INVENTORY_STORY_OBJ) \
		$(INVENTORY_HEADER_OBJ) tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(INVENTORY_MODULE_C_OBJ) $(INVENTORY_MODULE_ASM_OBJ) \
		$(INVENTORY_STORY_OBJ) $(INVENTORY_HEADER_OBJ)

$(INVENTORY_RESOLVER_OBJ): $(INVENTORY_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(INVENTORY_MODULE_RAW): $(INVENTORY_HEADER_OBJ) $(INVENTORY_MODULE_C_OBJ) \
		$(INVENTORY_MODULE_ASM_OBJ) $(INVENTORY_STORY_OBJ) \
		$(INVENTORY_RESOLVER_OBJ) $(INVENTORY_MODULE_CFG)
	$(LD65) -C $(INVENTORY_MODULE_CFG) -m $(OUTDIR)/inventory.map -o $@ \
		$(INVENTORY_HEADER_OBJ) $(INVENTORY_MODULE_C_OBJ) \
		$(INVENTORY_MODULE_ASM_OBJ) $(INVENTORY_STORY_OBJ) \
		$(INVENTORY_RESOLVER_OBJ)

$(INVENTORY_MODULE): $(INVENTORY_MODULE_RAW) \
		tools/finalize_inventory_overlay.py
	python3 tools/finalize_inventory_overlay.py --input $< \
		--map $(OUTDIR)/inventory.map --output $@

$(SAVELOAD_MODULE_C_OBJ): modules/saveload.c src/game.h src/platform.h src/world.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(SAVELOAD_DISK_OBJ): modules/disk_io.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SAVELOAD_HEADER_OBJ): modules/saveload_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SAVELOAD_RESOLVER_SRC): $(OUT_PRG) $(SAVELOAD_MODULE_C_OBJ) $(SAVELOAD_DISK_OBJ) \
		$(SAVELOAD_HEADER_OBJ) tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(SAVELOAD_MODULE_C_OBJ) $(SAVELOAD_DISK_OBJ) $(SAVELOAD_HEADER_OBJ)

$(SAVELOAD_RESOLVER_OBJ): $(SAVELOAD_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SAVELOAD_MODULE_RAW): $(SAVELOAD_HEADER_OBJ) $(SAVELOAD_MODULE_C_OBJ) \
		$(SAVELOAD_DISK_OBJ) $(SAVELOAD_RESOLVER_OBJ) $(SAVELOAD_MODULE_CFG)
	$(LD65) -C $(SAVELOAD_MODULE_CFG) -m $(OUTDIR)/saveload.map -o $@ \
		$(SAVELOAD_HEADER_OBJ) $(SAVELOAD_MODULE_C_OBJ) \
		$(SAVELOAD_DISK_OBJ) $(SAVELOAD_RESOLVER_OBJ)

$(SAVELOAD_MODULE): $(SAVELOAD_MODULE_RAW) \
		tools/finalize_inventory_overlay.py
	python3 tools/finalize_inventory_overlay.py --input $< \
		--map $(OUTDIR)/saveload.map --magic SL --output $@

$(SAVELOAD_SAVE_MODULE_C_OBJ): modules/saveload_save.c src/game.h src/platform.h src/world.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(SAVELOAD_SAVE_HEADER_OBJ): modules/saveload_save_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SAVELOAD_SAVE_RESOLVER_SRC): $(OUT_PRG) $(SAVELOAD_SAVE_MODULE_C_OBJ) $(SAVELOAD_DISK_OBJ) \
		$(SAVELOAD_SAVE_HEADER_OBJ) tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(SAVELOAD_SAVE_MODULE_C_OBJ) $(SAVELOAD_DISK_OBJ) $(SAVELOAD_SAVE_HEADER_OBJ)

$(SAVELOAD_SAVE_RESOLVER_OBJ): $(SAVELOAD_SAVE_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SAVELOAD_SAVE_MODULE_RAW): $(SAVELOAD_SAVE_HEADER_OBJ) $(SAVELOAD_SAVE_MODULE_C_OBJ) \
		$(SAVELOAD_DISK_OBJ) $(SAVELOAD_SAVE_RESOLVER_OBJ) $(SAVELOAD_SAVE_MODULE_CFG)
	$(LD65) -C $(SAVELOAD_SAVE_MODULE_CFG) -m $(OUTDIR)/saveload-save.map -o $@ \
		$(SAVELOAD_SAVE_HEADER_OBJ) $(SAVELOAD_SAVE_MODULE_C_OBJ) \
		$(SAVELOAD_DISK_OBJ) $(SAVELOAD_SAVE_RESOLVER_OBJ)

$(SAVELOAD_SAVE_MODULE): $(SAVELOAD_SAVE_MODULE_RAW) \
		tools/finalize_inventory_overlay.py
	python3 tools/finalize_inventory_overlay.py --input $< \
		--map $(OUTDIR)/saveload-save.map --magic SV --output $@

$(ROOM_HELPERS_C_OBJ): modules/room_helpers.c src/platform.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(ROOM_HELPERS_HEADER_OBJ): modules/room_helpers_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(ROOM_HELPERS_RESOLVER_SRC): $(OUT_PRG) $(ROOM_HELPERS_C_OBJ) \
		$(ROOM_HELPERS_HEADER_OBJ) tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(ROOM_HELPERS_C_OBJ) $(ROOM_HELPERS_HEADER_OBJ)

$(ROOM_HELPERS_RESOLVER_OBJ): $(ROOM_HELPERS_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(ROOM_HELPERS_MODULE_RAW): $(ROOM_HELPERS_HEADER_OBJ) $(ROOM_HELPERS_C_OBJ) \
		$(ROOM_HELPERS_RESOLVER_OBJ) $(ROOM_HELPERS_MODULE_CFG)
	$(LD65) -C $(ROOM_HELPERS_MODULE_CFG) -m $(OUTDIR)/room-helpers.map -o $@ \
		$(ROOM_HELPERS_HEADER_OBJ) $(ROOM_HELPERS_C_OBJ) \
		$(ROOM_HELPERS_RESOLVER_OBJ)

$(ROOM_HELPERS_MODULE): $(ROOM_HELPERS_MODULE_RAW) \
		tools/finalize_inventory_overlay.py
	python3 tools/finalize_inventory_overlay.py --input $< \
		--map $(OUTDIR)/room-helpers.map --magic RH --output $@

$(SCRIPT_C_OBJ): modules/script.c src/game.h src/platform.h | $(OUTDIR)
	$(CL65) $(CFLAGS) -Isrc -c -o $@ $<

$(SCRIPT_HEADER_OBJ): modules/script_header.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SCRIPT_RESOLVER_SRC): $(OUT_PRG) $(SCRIPT_C_OBJ) \
		$(SCRIPT_HEADER_OBJ) tools/generate_room_resolver.py | $(OUTDIR)
	python3 tools/generate_room_resolver.py --labels $(OUT_LBL) --output $@ \
		$(SCRIPT_C_OBJ) $(SCRIPT_HEADER_OBJ)

$(SCRIPT_RESOLVER_OBJ): $(SCRIPT_RESOLVER_SRC)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(SCRIPT_MODULE_RAW): $(SCRIPT_HEADER_OBJ) $(SCRIPT_C_OBJ) \
		$(SCRIPT_RESOLVER_OBJ) $(SCRIPT_MODULE_CFG)
	$(LD65) -C $(SCRIPT_MODULE_CFG) -m $(OUTDIR)/script.map -o $@ \
		$(SCRIPT_HEADER_OBJ) $(SCRIPT_C_OBJ) \
		$(SCRIPT_RESOLVER_OBJ)

$(SCRIPT_MODULE): $(SCRIPT_MODULE_RAW) \
		tools/finalize_inventory_overlay.py
	python3 tools/finalize_inventory_overlay.py --input $< \
		--map $(OUTDIR)/script.map --magic SC --output $@

$(DISK_BOOT_OBJ): disk/boot.s | $(OUTDIR)
	$(CL65) $(CFLAGS) -c -o $@ $<

$(DISK_BOOT_PRG): $(DISK_BOOT_OBJ) $(DISK_BOOT_CFG)
	$(LD65) -C $(DISK_BOOT_CFG) -m $(OUTDIR)/disk-boot.map -o $@ $<

$(ROOM_OUTDIR)/room-%.o: rooms/%.c src/game.h src/platform.h src/story.h | $(ROOM_OUTDIR)
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

$(OUT_EF_BIN): $(OUT_EF_BASE) $(TEXT_MODULE_PRG) $(INVENTORY_MODULE) $(SAVELOAD_MODULE) \
		$(SAVELOAD_SAVE_MODULE) $(ROOM_HELPERS_MODULE) $(SCRIPT_MODULE) tools/pack_easyflash.py \
		$(C64_ROOM_ASSETS) $(C64_OBJECT_TYPES) $(C64_PORTRAIT_ASSETS) $(C64_RESOURCE_ASSETS) \
		$(ROOM_CODES)
	python3 tools/pack_easyflash.py --base $(OUT_EF_BASE) --assets $(C64_ASSET_OUTDIR) \
		--objects $(C64_OBJECT_TYPES) --room-code $(ROOM_OUTDIR) \
		--inventory $(INVENTORY_MODULE) --saveload $(SAVELOAD_MODULE) \
		--saveload-save $(SAVELOAD_SAVE_MODULE) --room-helpers $(ROOM_HELPERS_MODULE) \
		--script $(SCRIPT_MODULE) \
		--output $@

$(OUT_CRT): $(OUT_EF_BIN)
	$(CARTCONV) -p -t easy -i $< -o $@ -n "$(CART_NAME)"

cartridge: $(OUT_CRT)

d64: $(OUT_D64)

$(OUT_D64): $(OUT_PRG) $(TEXT_MODULE_PRG) $(INVENTORY_MODULE) $(DISK_BOOT_PRG) \
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

$(SAVE_DISK): | $(OUTDIR)
	$(C1541) -format "$(SAVE_DISK_NAME),00" d64 $@

run-cartridge: $(OUT_CRT) $(SAVE_DISK)
	$(VICE) -cartcrt $(OUT_CRT) -8 $(SAVE_DISK)

asset-editor:
	python3 tools/asset-editor/server.py --host $(ASSET_EDITOR_HOST) --port $(ASSET_EDITOR_PORT)

clean:
	rm -rf $(OUTDIR)
