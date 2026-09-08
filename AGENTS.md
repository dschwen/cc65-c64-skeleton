# AGENTS.md

## Purpose
This repository is a starter skeleton for Commodore 64 game development with `cc65`.

Use this file for quick operational rules. For platform details, memory map notes, and cc65/C64 gotchas, read `GUIDE_cc65_C64.md`.

## Build and run
- Build PRG: `make`
- Build D64: `make d64`
- Run PRG in VICE: `make run`
- Run D64 in VICE: `make run-d64`
- This is a headless environment. Run VICE through Xvfb, using
  `xvfb-run -a x64sc ...` for direct emulator/debugging commands.

Build outputs are in `build/`:
- `game.prg`
- `game.map`
- `game.lbl`
- `game.d64` (when using `make d64`)

## Disk image extras
`make d64` writes the main program, room/type assets, and optional files from
`res/`.

Configurable variables:
- `RES_DIR` (default: `res`)
- `DISK_EXTRA_FILES` (default: `$(RES_DIR)/*`, hexadecimal room assets, and
  `assets/objects.cobj`)
- `DISK_NAME` (default disk label: `GAME`)
- `PRG_NAME` (default program filename on disk: `GAME`)

Examples:
- `make d64`
- `make RES_DIR=assets d64`
- `make DISK_EXTRA_FILES= d64`

## Project structure
- `src/`: C and assembly source files
- `cfg/`: linker configuration (`myc64.cfg`)
- `assets/`: optional binary assets
- `res/`: optional files copied into disk images

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
