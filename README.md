# C64 RPG / Adventure

An Oscar64 C/C++ game targeting an EasyFlash cartridge. This repository
started as a cc65 skeleton; the active build now uses Oscar64.

## Build and run

Requires Oscar64, Python 3, VICE (`x64sc`, `c1541`) and Xvfb.

```sh
make OSCAR64=/home/daniel/Programs/oscar64/build/oscar64
make OSCAR64=/home/daniel/Programs/oscar64/build/oscar64 run
```

The build emits `build/game.crt`, `game.map`, `game.lbl` and `game.asm`.
`make cartridge` is an alias for the default build. `make run` and
`make run-cartridge` create/attach `build/saves.d64` as unit 8.
Override `SAVE_DISK` or `SAVE_DISK_NAME` to use another save disk.
Set `XVFB_RUN=` to run VICE on an existing desktop display.

## Regression test

Install `python3-xlib`, then run:

```sh
make OSCAR64=/home/daniel/Programs/oscar64/build/oscar64 test
```

The test cold-boots the cartridge, compares high-RAM assets, checks
KERNAL/CIA setup, sends real Xvfb key events, exercises Look/inventory/
movement, verifies the visible raster split and tests room environments.
It prints an artifact directory containing logs and screenshots.

## Development references

- `MEMORY_MAP.md`: current memory ownership, banking and startup contract.
- `PLATFORM_API.md`: C platform API and binary contracts.
- `EASYFLASH_CARTRIDGE.md`: current build and historical cartridge notes.
- `GAME_DESIGNER_GUIDE.md`: story and map design.
- `ROOM_CODE_API.md` and `STORY_CODE_API.md`: room/story behavior.
- `GUIDE_cc65_C64.md`: C64 hardware notes and historical cc65 details.
- `tools/asset-editor/README.md`: HTML asset editor and asset formats.

Memory placement is expressed by Oscar64 pragmas in `src/memmap.h`
and the banked source files. Charset/sprite data remain in VIC bank 3
beneath KERNAL ROM, leaving main RAM available for code and game state.
