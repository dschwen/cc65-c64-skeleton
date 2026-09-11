# cc65 C64 Skeleton

This is a minimal cc65-based C64 repository skeleton with:
- A non-programmer guide for story and map design: `GAME_DESIGNER_GUIDE.md`
- A pragmatic guide: `GUIDE_cc65_C64.md`
- C platform API and binary contracts: `PLATFORM_API.md`
- Runtime RAM, banking, and sprite ownership: `MEMORY_MAP.md`
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
- `build/IV` (inventory UI and story-specific in-place cartridge service)
- `build/RH` (in-place room-neighbor/object-removal service)
- `build/game.map`
- `build/game.lbl`

`game.prg` is the resident engine and `text.prg` is the independently loaded
bottom-text pager. The cartridge target is the complete runtime. The D64 target
currently exercises the legacy disk bootstrap/fallback path; it packages room
files but the engine does not yet provide a disk-backed replacement for the
EasyFlash resource and banked-service APIs.

Build disk image:
```bash
make d64
```

Output:
- `build/game.d64`
- `build/disk-boot.prg` (the first-file loader stored as `GAME`)
- plus `res/*` and build-prepared PETSCII copies of the hexadecimal room and
  object-type assets

The disk loader relocates itself to `$0200`, loads `ENGINE` at its normal PRG
address, loads the helper/text module `TEXT` at `$3A00`, and then enters cc65
startup at `$080D`. This packaging path is not currently playable without the
EasyFlash-only resource and service backends described above.

Build an EasyFlash cartridge image:
```bash
make cartridge
```

See `EASYFLASH_CARTRIDGE.md` for the boot process, linker layout, CRT format,
validation steps, multi-bank expansion guidance, and flash-save constraints.
See `ROOM_CODE_API.md` for per-room C handlers, `GameState`, and the room-code
ABI.
See `STORY_CODE_API.md` for map Use hooks and global inventory-item behavior.

Output:
- `build/game.crt`
- `build/saves.d64` (created by `make run-cartridge`, retained between runs)
- `build/game-ef-base.bin` (raw executable banks 0-2)
- `build/game-ef.bin` (packed executable and runtime asset banks)

## Run
Run via Makefile targets:
```bash
make run      # builds and autostarts the legacy D64 fallback
make run-d64  # same explicit disk workflow
make run-cartridge # attaches game.crt and persistent build/saves.d64
```

Useful overrides:
```bash
make VICE=x64
make DISK_NAME=MYGAME PRG_NAME=MYGAME d64
make SAVE_DISK=/path/to/saves.d64 run-cartridge
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
