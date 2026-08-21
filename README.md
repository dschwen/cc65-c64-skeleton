# cc65 C64 Skeleton

This is a minimal cc65-based C64 repository skeleton with:
- A pragmatic guide: `GUIDE_cc65_C64.md`
- C platform API and binary contracts: `PLATFORM_API.md`
- EasyFlash cartridge notes: `EASYFLASH_CARTRIDGE.md`
- Minimal VIC/SID helper wrappers
- A custom linker config starter: `cfg/myc64.cfg`
- A Makefile that builds a `.prg` plus `.map`

## Requirements
- cc65 toolchain installed (`cl65`, `ca65`, `ld65` available on PATH)
- VICE tools for optional run/disk workflows:
  - emulator (`x64sc` by default)
  - disk utility (`c1541`) for `.d64` creation
  - `cartconv` for EasyFlash `.crt` creation

## Build
```bash
make
```

Outputs:
- `build/game.prg`
- `build/text.prg`
- `build/game.map`
- `build/game.lbl`

`game.prg` is the resident engine and `text.prg` is the independently loaded
bottom-text pager. Use the D64 or cartridge target to run the complete game;
the engine PRG alone deliberately does not contain a padded copy of the pager.

Build disk image:
```bash
make d64
```

Output:
- `build/game.d64`
- `build/disk-boot.prg` (the first-file loader stored as `GAME`)
- plus `res/*`, hexadecimal room assets, and `assets/objects.cobj`

The disk loader relocates itself to `$0200`, loads `ENGINE` at its normal PRG
address, loads `TEXT` at `$B880`, and then enters cc65 startup at `$080D`.

Build an EasyFlash cartridge image:
```bash
make cartridge
```

See `EASYFLASH_CARTRIDGE.md` for the boot process, linker layout, CRT format,
validation steps, multi-bank expansion guidance, and flash-save constraints.
See `ROOM_CODE_API.md` for per-room C handlers, `GameState`, and the overlay
ABI.

Output:
- `build/game.crt`
- `build/game-ef-base.bin` (raw executable banks 0-2)
- `build/game-ef.bin` (packed executable and runtime asset banks)

## Run
Run via Makefile targets:
```bash
make run      # builds and autostarts the complete D64
make run-d64  # same explicit disk workflow
make run-cartridge # attaches build/game.crt
```

Useful overrides:
```bash
make VICE=x64
make DISK_NAME=MYGAME PRG_NAME=MYGAME d64
make RES_DIR=assets d64
make DISK_EXTRA_FILES= d64
```

## Notes
- This project is intentionally small; extend by adding segments and asset placement rules in `cfg/myc64.cfg`.
- HTML asset editor: `tools/asset-editor/`, with format details in `tools/asset-editor/README.md`.
- Run the asset editor with disk-backed asset open/save:
```bash
make asset-editor
```

Then open `http://127.0.0.1:8000/`.
