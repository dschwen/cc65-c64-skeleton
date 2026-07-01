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
DISK_NAME ?= GAME
PRG_NAME  ?= GAME
RES_DIR ?= res
DISK_EXTRA_FILES ?= $(wildcard $(RES_DIR)/*)
DISK_EXTRA_DEPS := $(wildcard $(DISK_EXTRA_FILES))
ASSET_EDITOR_HOST ?= 127.0.0.1
ASSET_EDITOR_PORT ?= 8000

CFLAGS := -t $(TARGET) -Oirs --cpu 6502
LDFLAGS := -C $(CFG)

SOURCES_C := $(wildcard src/*.c)
SOURCES_S := $(wildcard src/*.s)

OUT_PRG := $(OUTDIR)/game.prg
OUT_MAP := $(OUTDIR)/game.map
OUT_LBL := $(OUTDIR)/game.lbl
OUT_D64 := $(OUTDIR)/game.d64

.PHONY: all clean d64 run run-d64 asset-editor

all: $(OUT_PRG)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUT_PRG): $(SOURCES_C) $(SOURCES_S) | $(OUTDIR)
	$(CL65) $(CFLAGS) $(LDFLAGS) -m $(OUT_MAP) -Ln $(OUT_LBL) -o $@ $(SOURCES_C) $(SOURCES_S)

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

asset-editor:
	python3 tools/asset-editor/server.py --host $(ASSET_EDITOR_HOST) --port $(ASSET_EDITOR_PORT)

clean:
	rm -rf $(OUTDIR)
