# cc65 C64 Skeleton

This is a minimal cc65-based C64 repository skeleton with:
- A pragmatic guide: `GUIDE_cc65_C64.md`
- Minimal VIC/SID helper wrappers
- A custom linker config starter: `cfg/myc64.cfg`
- A Makefile that builds a `.prg` plus `.map`

## Requirements
- cc65 toolchain installed (`cl65`, `ca65`, `ld65` available on PATH)
- VICE tools for optional run/disk workflows:
  - emulator (`x64sc` by default)
  - disk utility (`c1541`) for `.d64` creation

## Build
```bash
make
```

Outputs:
- `build/game.prg`
- `build/game.map`
- `build/game.lbl`

Build disk image:
```bash
make d64
```

Output:
- `build/game.d64`
- plus any files found in `res/` (configurable via `DISK_EXTRA_FILES`)

## Run
Open `build/game.prg` in your favorite C64 emulator (VICE etc.).

Or run via Makefile targets:
```bash
make run      # autostarts build/game.prg
make run-d64  # boots build/game.d64 as drive 8
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
