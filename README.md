# cc65 C64 Skeleton

This is a minimal cc65-based C64 repository skeleton with:
- A pragmatic guide: `GUIDE_cc65_C64.md`
- Minimal VIC/SID helper wrappers
- A custom linker config starter: `cfg/myc64.cfg`
- A Makefile that builds a `.prg` plus `.map`

## Requirements
- cc65 toolchain installed (`cl65`, `ca65`, `ld65` available on PATH)

## Build
```bash
make
```

Outputs:
- `build/game.prg`
- `build/game.map`
- `build/game.lbl`

## Run
Open `build/game.prg` in your favorite C64 emulator (VICE etc.).

## Notes
- This project is intentionally small; extend by adding segments and asset placement rules in `cfg/myc64.cfg`.
