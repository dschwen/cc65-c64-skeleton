# oscar64 C64/EasyFlash Makefile.
#
# Replaces the old cc65/cl65/ld65/cartconv pipeline: oscar64 is a whole-
# program compiler (one invocation compiles *and* links everything, and
# -tf=crt emits the EasyFlash .crt directly), so there are no separate
# object files, no per-bank link steps, no linker .cfg files, and no
# cartconv pass. What remains is asset preparation (PETSCII conversion,
# the object-type hot/cold split, and compiling the .rc room-code DSL to
# C) - the same three tools/*.py scripts as before, feeding oscar64
# #embed directives instead of ca65 .incbin.
#
OSCAR64 ?= oscar64
VICE    ?= x64sc
C1541   ?= c1541
XVFB_RUN ?= xvfb-run -a
SAVE_DISK ?= build/saves.d64
SAVE_DISK_NAME ?= SAVES
OUTDIR  := build

OFLAGS := -n -Os -xz -tf=crt

SRC_C := \
	src/main.c \
	src/resources.c \
	src/platform.c \
	src/render.c \
	src/irq.c \
	src/banking.c \
	src/dirty_redraw.c \
	src/text.c \
	src/input.c \
	src/env_dispatch.c \
	src/hero_light.c \
	src/room_dispatch.c \
	src/assets_data.c \
	src/object_types_data.c \
	src/disk_io.c \
	src/game.c \
	src/game_support.c \
	src/world.c \
	story/story.c \
	modules/typeinfo.c \
	modules/room_helpers.c \
	modules/look_helpers.c \
	modules/inventory.c \
	modules/saveload.c \
	modules/saveload_save.c \
	modules/script.c \
	src/script_runtime.c \
	rooms/env/rain.c \
	rooms/env/tavern.c \
	rooms/env/tavern_muffled.c \
	rooms/env/cellar.c

INCLUDES := -i=src -i=modules -i=rooms -i=$(OUTDIR)/rooms

ROOM_IDS := 00 01 02 03 04 05
ROOM_ASSETS := $(addprefix $(OUTDIR)/assets/,$(ROOM_IDS))
ROOM_SOURCES := $(addprefix $(OUTDIR)/rooms/room,$(addsuffix .c,$(ROOM_IDS)))

OBJECTS_HOT  := $(OUTDIR)/assets/objects-hot.bin
OBJECTS_COLD := $(OUTDIR)/assets/objects-cold.bin

# All rooms' compiled script bytecode shares one EasyFlash bank (see
# src/banks.h's BANK_SCRIPT_ROOM_FIRST and src/resources.c's header
# comment for why one shared bank beats one bank per resource).
SCRIPT_BINS      := $(addprefix $(OUTDIR)/scripts/,$(addsuffix .bin,$(ROOM_IDS)))
SCRIPT_BANK_SRC  := $(OUTDIR)/scripts/script_room_bank.c
SCRIPT_BANK_FIRST := 32

# Conversations (src/banks.h's BANK_SCRIPT_CONVERSATION_FIRST): one .script
# per conversation ID under assets/scripts/conversations/, bundled into
# their own shared bank the same way room scripts are - see that constant's
# comment for why conversations don't share the room scripts' bank.
CONVERSATION_SOURCES := $(wildcard assets/scripts/conversations/*.script)
CONVERSATION_IDS     := $(basename $(notdir $(CONVERSATION_SOURCES)))
CONVERSATION_BINS    := $(addprefix $(OUTDIR)/scripts/conversations/,$(addsuffix .bin,$(CONVERSATION_IDS)))
CONVERSATION_BANK_SRC := $(OUTDIR)/scripts/script_conversation_bank.c
CONVERSATION_BANK_FIRST := 33

# Portraits (src/banks.h's BANK_PORTRAIT_FIRST): raw 256-byte binary assets,
# assets/portraits/<id> (tools/asset-editor) - no DSL compile step, fed
# straight into the same bank generator as room scripts/conversations.
PORTRAIT_SOURCES := $(wildcard assets/portraits/*)
PORTRAIT_IDS     := $(notdir $(PORTRAIT_SOURCES))
PORTRAIT_BANK_SRC := $(OUTDIR)/scripts/portrait_bank.c
PORTRAIT_BANK_FIRST := 24

SCRIPT_SOURCES   := $(SCRIPT_BANK_SRC) $(if $(CONVERSATION_IDS),$(CONVERSATION_BANK_SRC)) \
                    $(if $(PORTRAIT_IDS),$(PORTRAIT_BANK_SRC))

OUT_CRT := $(OUTDIR)/game.crt

.PHONY: all cartridge run run-cartridge test clean
.DEFAULT_GOAL := all
# Script .bin files are an intermediate step whose generated .c wrapper
# (below) embeds a path to them - without .PRECIOUS, Make deletes the .bin
# after building the .c from it, leaving that .c's #embed pointing at a
# file that no longer exists (confirmed live: a subsequent build then finds
# the .c "up to date" - it exists and is newer than nothing - and only
# oscar64's own #embed fails, confusingly far from the real cause).
.PRECIOUS: $(OUTDIR)/scripts/%.bin $(OUTDIR)/scripts/conversations/%.bin $(OUTDIR)/assets/%

all cartridge: $(OUT_CRT)

$(OUTDIR)/assets $(OUTDIR)/rooms $(OUTDIR)/scripts $(OUTDIR)/scripts/conversations:
	mkdir -p $@

# Room data: PETSCII-convert the editor-authored room file (assets/NN) into
# the build tree, embedded by build/rooms/roomNN.c below.
$(OUTDIR)/assets/%: assets/% tools/prepare_c64_assets.py | $(OUTDIR)/assets
	python3 tools/prepare_c64_assets.py room $< $@

# Object types: PETSCII-convert then split into the resident "hot" part
# (PlatformObjectType, always visible) and the banked "cold" part
# (PlatformObjectTypeInfo, fetched only for Look/Take text) - see
# src/object_types_data.c and modules/typeinfo.c.
$(OBJECTS_HOT) $(OBJECTS_COLD) &: assets/objects.cobj tools/prepare_c64_assets.py | $(OUTDIR)/assets
	python3 tools/prepare_c64_assets.py objects-split $< $(OBJECTS_HOT) --cold-output $(OBJECTS_COLD)

# Room code: rooms/NN.rc (the room-code DSL) compiles straight to oscar64
# C, embedding the room's own prepared data - see tools/compile_room.py
# and ROOM_CODE_API.md's "Room-code DSL (.rc files)" section.
$(OUTDIR)/rooms/room%.c: rooms/%.rc $(OUTDIR)/assets/% tools/compile_room.py src/story.h src/game.h $(wildcard rooms/c/*.c) | $(OUTDIR)/rooms
	python3 tools/compile_room.py --input $< --output $@ --room-id 0x$* --room-data $(OUTDIR)/assets/$*

# Room script bytecode (tools/compile_script.py's DSL - see its module
# docstring): compiled once per room from assets/scripts/NN.script, then
# all six wrapped together in one shared EasyFlash bank as #embed'd const
# arrays (tools/generate_script_bank.py) - see src/resources.c's header
# comment for why one shared bank replaces the old shared-directory/
# checksum design more cheaply than one bank per resource would.
$(OUTDIR)/scripts/%.bin: assets/scripts/%.script tools/compile_script.py src/story.h | $(OUTDIR)/scripts
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind room --flags src/story.h

$(SCRIPT_BANK_SRC): $(SCRIPT_BINS) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(ROOM_IDS),--input $(id)=$(OUTDIR)/scripts/$(id).bin) \
		--output $@ --bank-symbol script_room --fetch-fn script_room_bank_fetch \
		--bank $(SCRIPT_BANK_FIRST)

# Conversation bytecode: same shape as the room-script rules above, one
# level deeper (assets/scripts/conversations/NN.script) and its own bank.
$(OUTDIR)/scripts/conversations/%.bin: assets/scripts/conversations/%.script tools/compile_script.py src/story.h | $(OUTDIR)/scripts/conversations
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind conversation --flags src/story.h

$(CONVERSATION_BANK_SRC): $(CONVERSATION_BINS) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(CONVERSATION_IDS),--input $(id)=$(OUTDIR)/scripts/conversations/$(id).bin) \
		--output $@ --bank-symbol script_conversation --fetch-fn script_conversation_bank_fetch \
		--bank $(CONVERSATION_BANK_FIRST)

$(PORTRAIT_BANK_SRC): $(PORTRAIT_SOURCES) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(PORTRAIT_IDS),--input $(id)=assets/portraits/$(id)) \
		--output $@ --bank-symbol portrait --fetch-fn portrait_bank_fetch \
		--bank $(PORTRAIT_BANK_FIRST)

HEADERS := $(wildcard src/*.h modules/*.h rooms/*.h rooms/env/*.h)

$(OUT_CRT): Makefile $(SRC_C) $(HEADERS) $(ROOM_ASSETS) $(ROOM_SOURCES) $(SCRIPT_SOURCES) $(OBJECTS_HOT) $(OBJECTS_COLD) assets/charset.cchr assets/tiles.ctil | $(OUTDIR)
	$(OSCAR64) $(OFLAGS) $(INCLUDES) -i=$(OUTDIR)/scripts $(SRC_C) $(ROOM_SOURCES) $(SCRIPT_SOURCES) -o=$@
	python3 tools/check_bank_switches.py $(OUTDIR)/game.asm || { rm -f $@; exit 1; }

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(SAVE_DISK):
	mkdir -p $(dir $@)
	$(C1541) -format "$(SAVE_DISK_NAME),00" d64 "$@"

run run-cartridge: $(OUT_CRT) $(SAVE_DISK)
	$(XVFB_RUN) $(VICE) -cartcrt $(OUT_CRT) -8 "$(SAVE_DISK)"

test: $(OUT_CRT)
	$(XVFB_RUN) python3 tools/test_oscar_boot.py --vice "$(VICE)" --c1541 "$(C1541)"

clean:
	rm -rf $(OUTDIR)
