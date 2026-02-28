# Minimal cc65 C64 Makefile
# Override CC65_HOME if you need to point at a custom install.
# Example: make CC65_HOME=/opt/cc65

CC65_HOME ?=
CL65 := $(if $(CC65_HOME),$(CC65_HOME)/bin/cl65,cl65)

TARGET := c64
OUTDIR := build
CFG    := cfg/myc64.cfg

CFLAGS := -t $(TARGET) -Oirs --cpu 6502
LDFLAGS := -C $(CFG)

SOURCES_C := $(wildcard src/*.c)
SOURCES_S := $(wildcard src/*.s)

OUT_PRG := $(OUTDIR)/game.prg
OUT_MAP := $(OUTDIR)/game.map
OUT_LBL := $(OUTDIR)/game.lbl

.PHONY: all clean

all: $(OUT_PRG)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUT_PRG): $(SOURCES_C) $(SOURCES_S) | $(OUTDIR)
	$(CL65) $(CFLAGS) $(LDFLAGS) -m $(OUT_MAP) -Ln $(OUT_LBL) -o $@ $(SOURCES_C) $(SOURCES_S)

clean:
	rm -rf $(OUTDIR)
