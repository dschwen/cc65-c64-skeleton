# AGENTS.md

## Purpose
This repository is a starter skeleton for Commodore 64 game development with `cc65`.

Use this file for quick operational rules. For platform details, memory map notes, and cc65/C64 gotchas, read `GUIDE_cc65_C64.md`.

## Build and run
- Build PRG: `make`
- Build EasyFlash CRT: `make cartridge`
- Run cartridge in VICE: `make run`
- Run EasyFlash in VICE: `make run-cartridge`
- This is a headless environment. Run VICE through Xvfb, using
  `xvfb-run -a x64sc ...` for direct emulator/debugging commands.

Build outputs are in `build/`:
- `game.prg`
- `game.map`
- `game.lbl`
- `game.crt` (when using `make cartridge`)

## Save disk
The game runtime is EasyFlash-only. Disk is used only for save games.
`make run`/`make run-cartridge` creates `build/saves.d64` when needed and
attaches it as unit 8.

Configurable variables:
- `SAVE_DISK` (default: `build/saves.d64`)
- `SAVE_DISK_NAME` (default disk label: `SAVES`)

Examples:
- `make run`
- `make SAVE_DISK=/path/to/saves.d64 run-cartridge`

## Project structure
- `src/`: C and assembly source files
- `cfg/`: linker configuration (`myc64.cfg`)
- `assets/`: optional binary assets

## Change guidance
- Keep the Makefile simple and override-friendly via variables.
- Preserve `.map` generation for memory/layout debugging.
- When changing memory layout or fixed addresses, update `cfg/myc64.cfg` and
  keep `MEMORY_MAP.md` (the authoritative allocation table) in sync, plus the
  summary tables in `GUIDE_cc65_C64.md` and `PLATFORM_API.md`.
- Code can run directly from an EasyFlash bank via `FAR_CALL`. Such a routine
  may only read `$0000-$7FFF` and `$C000-$CFFF`, and may only call resident
  code outside `$8000-$BFFF`, because the cartridge covers that window while
  the call runs. See `PLATFORM_API.md`'s "Banked code"; ongoing plan and dead
  ends are in `MEMORY_MAP_TARGET.md`.
