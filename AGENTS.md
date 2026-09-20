# AGENTS.md

## Purpose
This repository is a Commodore 64 RPG/adventure using Oscar64 and EasyFlash.

Use this file for quick operational rules. Read `MEMORY_MAP.md` for the
current layout and boot contract. `GUIDE_cc65_C64.md` retains historical
cc65 notes alongside a current Oscar64 layout summary.

## Build and run
- Build EasyFlash CRT: `make` or `make cartridge`
- Set `OSCAR64=/home/daniel/Programs/oscar64/build/oscar64` if it is not on PATH.
- Cold-boot/input/raster regression: `make test` (requires python3-xlib).
- Run cartridge in VICE: `make run`
- Run EasyFlash in VICE: `make run-cartridge`
- This is a headless environment. Run VICE through Xvfb, using
  `xvfb-run -a x64sc ...` for direct emulator/debugging commands.

Build outputs are in `build/`:
- `game.map`
- `game.lbl`
- `game.asm`
- `game.crt`

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
- When changing memory layout or fixed addresses, update `src/memmap.h` and
  keep `MEMORY_MAP.md` (the authoritative allocation table) in sync, plus the
  summary tables in `GUIDE_cc65_C64.md` and `PLATFORM_API.md`.
- Code runs directly from EasyFlash via `far_call_bank`. Keep entries
  `__noinline`; use its software bank shadow because `$DE00` is write-only.
  Resident code/state and both stacks must be outside `$8000-$BFFF` and
  below the environment window at `$7E00`. CPU reads beneath KERNAL must
  use the resident RAM gate; VIC assets stay in bank 3 beneath KERNAL.
