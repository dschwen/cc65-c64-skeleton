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
	modules/title.c \
	src/script_runtime.c \
	rooms/env/rain.c \
	rooms/env/tavern.c \
	rooms/env/tavern_muffled.c \
	rooms/env/cellar.c

INCLUDES := -i=src -i=modules -i=rooms -i=$(OUTDIR) -i=$(OUTDIR)/rooms

# The room list comes from rooms/rooms.manifest (one `<hex id> <environment>`
# line per room); tools/generate_room_table.py turns the same file into the
# dispatch table and environment lookup below.
ROOM_MANIFEST := rooms/rooms.manifest
ROOM_IDS := $(shell sed -e 's/\#.*//' $(ROOM_MANIFEST) | awk 'NF { print $$1 }')
ROOM_ASSETS := $(addprefix $(OUTDIR)/assets/,$(ROOM_IDS))
ROOM_SOURCES := $(addprefix $(OUTDIR)/rooms/room,$(addsuffix .c,$(ROOM_IDS)))
ROOM_TABLE_H := $(OUTDIR)/rooms/room_table.h
ENV_BANKS_H  := $(OUTDIR)/rooms/env_banks.h

OBJECTS_HOT  := $(OUTDIR)/assets/objects-hot.bin
OBJECTS_COLD := $(OUTDIR)/assets/objects-cold.bin

# OBJ_<NAME> symbols for every named object type (tools/generate_object_
# types.py) - included by generated room code and read as a symbol file by
# the script and room compilers, so sources name items instead of IDs.
OBJECT_TYPES_H := $(OUTDIR)/object_types.h
SYMBOL_FLAGS := --flags src/story.h --flags src/sounds.h --flags src/game.h --flags $(OBJECT_TYPES_H)

# All rooms' compiled script bytecode shares one EasyFlash bank (see
# src/banks.h's BANK_SCRIPT_ROOM_FIRST and src/resources.c's header
# comment for why one shared bank beats one bank per resource). The bank
# generator is handed the banks.h symbol, so the numbers live only there.
SCRIPT_BINS      := $(addprefix $(OUTDIR)/scripts/,$(addsuffix .bin,$(ROOM_IDS)))
SCRIPT_BANK_SRC  := $(OUTDIR)/scripts/script_room_bank.c

# Conversations (src/banks.h's BANK_SCRIPT_CONVERSATION_FIRST): one .script
# per conversation ID under assets/scripts/conversations/ (the file name is
# the *decimal* ID a script's `converse` names; room IDs are hex), bundled into
# their own shared bank the same way room scripts are - see that constant's
# comment for why conversations don't share the room scripts' bank.
CONVERSATION_SOURCES := $(wildcard assets/scripts/conversations/*.script)
CONVERSATION_IDS     := $(basename $(notdir $(CONVERSATION_SOURCES)))
CONVERSATION_BINS    := $(addprefix $(OUTDIR)/scripts/conversations/,$(addsuffix .bin,$(CONVERSATION_IDS)))
CONVERSATION_BANK_SRC := $(OUTDIR)/scripts/script_conversation_bank.c

# Portraits (src/banks.h's BANK_PORTRAIT_FIRST): raw 256-byte binary assets,
# assets/portraits/<id> (tools/asset-editor) - no DSL compile step, fed
# straight into the same bank generator as room scripts/conversations.
PORTRAIT_SOURCES := $(wildcard assets/portraits/*)
PORTRAIT_IDS     := $(notdir $(PORTRAIT_SOURCES))
PORTRAIT_BANK_SRC := $(OUTDIR)/scripts/portrait_bank.c

SCRIPT_SOURCES   := $(SCRIPT_BANK_SRC) $(if $(CONVERSATION_IDS),$(CONVERSATION_BANK_SRC)) \
                    $(if $(PORTRAIT_IDS),$(PORTRAIT_BANK_SRC))

OUT_CRT := $(OUTDIR)/game.crt

.PHONY: all cartridge run run-cartridge test clean title-music run-title-music
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

$(OBJECT_TYPES_H): assets/objects.cobj tools/generate_object_types.py | $(OUTDIR)
	python3 tools/generate_object_types.py --input $< --output $@

$(ROOM_TABLE_H) $(ENV_BANKS_H) &: $(ROOM_MANIFEST) tools/generate_room_table.py | $(OUTDIR)/rooms
	python3 tools/generate_room_table.py --manifest $< --room-table $(ROOM_TABLE_H) --env-banks $(ENV_BANKS_H)

# Room code: rooms/NN.rc (the room-code DSL) compiles straight to oscar64
# C, embedding the room's own prepared data - see tools/compile_room.py
# and ROOM_CODE_API.md's "Room-code DSL (.rc files)" section.
$(OUTDIR)/rooms/room%.c: rooms/%.rc $(OUTDIR)/assets/% tools/compile_room.py src/story.h src/game.h $(OBJECT_TYPES_H) $(wildcard rooms/c/*.c) | $(OUTDIR)/rooms
	python3 tools/compile_room.py --input $< --output $@ --room-id 0x$* --room-data $(OUTDIR)/assets/$* \
		--flags src/story.h --flags src/game.h --flags $(OBJECT_TYPES_H)

# Room script bytecode (tools/compile_script.py's DSL - see its module
# docstring): compiled once per room from assets/scripts/NN.script, then
# all six wrapped together in one shared EasyFlash bank as #embed'd const
# arrays (tools/generate_script_bank.py) - see src/resources.c's header
# comment for why one shared bank replaces the old shared-directory/
# checksum design more cheaply than one bank per resource would.
$(OUTDIR)/scripts/%.bin: assets/scripts/%.script tools/compile_script.py src/story.h src/script_format.h $(OBJECT_TYPES_H) | $(OUTDIR)/scripts
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind room $(SYMBOL_FLAGS)

$(SCRIPT_BANK_SRC): $(SCRIPT_BINS) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(ROOM_IDS),--input $(shell printf %d 0x$(id))=$(OUTDIR)/scripts/$(id).bin) \
		--output $@ --bank-symbol script_room --fetch-fn script_room_bank_fetch \
		--bank BANK_SCRIPT_ROOM_FIRST

# Conversation bytecode: same shape as the room-script rules above, one
# level deeper (assets/scripts/conversations/NN.script) and its own bank.
$(OUTDIR)/scripts/conversations/%.bin: assets/scripts/conversations/%.script tools/compile_script.py src/story.h src/script_format.h $(OBJECT_TYPES_H) | $(OUTDIR)/scripts/conversations
	python3 tools/compile_script.py --input $< --single-output $@ --expect-kind conversation $(SYMBOL_FLAGS)

$(CONVERSATION_BANK_SRC): $(CONVERSATION_BINS) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(CONVERSATION_IDS),--input $(id)=$(OUTDIR)/scripts/conversations/$(id).bin) \
		--output $@ --bank-symbol script_conversation --fetch-fn script_conversation_bank_fetch \
		--bank BANK_SCRIPT_CONVERSATION_FIRST

$(PORTRAIT_BANK_SRC): $(PORTRAIT_SOURCES) tools/generate_script_bank.py | $(OUTDIR)/scripts
	python3 tools/generate_script_bank.py \
		$(foreach id,$(PORTRAIT_IDS),--input $(id)=assets/portraits/$(id)) \
		--output $@ --bank-symbol portrait --fetch-fn portrait_bank_fetch \
		--bank BANK_PORTRAIT_FIRST

HEADERS := $(wildcard src/*.h modules/*.h rooms/*.h rooms/env/*.h music/*.h)

$(OUT_CRT): Makefile $(SRC_C) $(HEADERS) $(ROOM_ASSETS) $(ROOM_SOURCES) $(ROOM_TABLE_H) $(ENV_BANKS_H) $(OBJECT_TYPES_H) $(SCRIPT_SOURCES) $(OBJECTS_HOT) $(OBJECTS_COLD) assets/charset.cchr assets/tiles.ctil assets/title.hires | $(OUTDIR)
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

# Standalone title-music preview (music/title_music.c): a plain PRG, not
# part of the cartridge.
TITLE_MUSIC_PRG := $(OUTDIR)/title_music.prg

title-music: $(TITLE_MUSIC_PRG)

$(TITLE_MUSIC_PRG): music/title_music.c $(wildcard music/*.h) | $(OUTDIR)
	$(OSCAR64) -n -O2 music/title_music.c -o=$@

run-title-music: $(TITLE_MUSIC_PRG)
	$(XVFB_RUN) $(VICE) -autostart $(TITLE_MUSIC_PRG)

clean:
	rm -rf $(OUTDIR)
