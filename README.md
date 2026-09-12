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
- VICE tools for cartridge runs and the save-disk workflow:
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
- `build/SC` (in-place script/conversation/room interpreter)
- `build/LH` (in-place Look/Take text service)
- `build/BT` (in-place cold object-type information service)
- `build/game.map`
- `build/game.lbl`

`game.prg` is the resident engine and `text.prg` is the independently loaded
bottom-text pager. The EasyFlash cartridge target is the complete runtime.
Disk is used only for the save image attached to unit 8.

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
make run           # cartridge run with persistent build/saves.d64
make run-cartridge # explicit form of the same workflow
```

Useful overrides:
```bash
make VICE=x64
make SAVE_DISK=/path/to/saves.d64 run-cartridge
```

## Notes
- This project is intentionally small; extend by adding segments and asset placement rules in `cfg/myc64.cfg`.
- HTML asset editor: `tools/asset-editor/`, with format details in `tools/asset-editor/README.md`.
- Run the asset editor with disk-backed asset open/save:
```bash
make asset-editor
```

Then open `http://127.0.0.1:8000/`.
