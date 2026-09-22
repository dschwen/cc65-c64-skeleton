# AGENTS.md

## Purpose

This repository is a Commodore 64 RPG/adventure using Oscar64 and EasyFlash.
Read [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) for an agent-oriented summary
of the local compiler manual. [MEMORY_MAP.md](MEMORY_MAP.md) is the
authoritative allocation; [EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md)
covers project-specific boot and banking.

## Build, run and test

- Build CRT: `make` or `make cartridge`.
- Set `OSCAR64=/home/daniel/Programs/oscar64/build/oscar64` if not on PATH.
- Run: `make run` or `make run-cartridge`.
- Regression: `make test` (requires VICE, Xvfb, python3-xlib and c1541); it
  includes an F1 save / F3 load round trip on a disposable D64.
- Direct VICE commands must use `xvfb-run -a x64sc ...` in this headless environment.
- Send test keypresses via Xvfb/XTest, not KERNAL keyboard-buffer injection.
- Outputs: `build/game.crt`, `game.map`, `game.lbl`, `game.asm`.
- `make clean` removes `build/`, including the default save disk; preserve saves first.

Runtime assets come from EasyFlash. Disk is only for saves.
Run targets create/attach device 8 using `SAVE_DISK` (default
`build/saves.d64`) and `SAVE_DISK_NAME` (default `SAVES`).
Example: `make SAVE_DISK=/path/to/saves.d64 run-cartridge`.

## Source locations

- `src/`: resident engine, hardware wrappers, memory and bank declarations.
- `modules/`: banked services; `rooms/*.rc`: room DSL;
  `rooms/c/`: C handler fragments; `rooms/env/`: relocated environments.
- `assets/`: editor data and script sources; `tools/`: generators and tests.
- `src/memmap.h`, `src/banks.h` and source pragmas define placement.
  There is no separate linker configuration or cartridge-packing step.
- Generated room/script C and prepared assets live in `build/`; edit their sources.

## Change guidance

- Keep Makefile variables override-friendly and preserve map/listing generation.
- Update memory/bank declarations and MEMORY_MAP when changing allocation;
  keep the guide, cartridge notes and relevant API docs consistent.
- Whole-program optimization can inline across boundaries: mark banked entries
  `__noinline`, leave mutable state resident, and use `far_call_bank`.
- Restore banks through `platform_cart_bank`; `$DE00` is write-only.
- `far_call_bank()` may only be expanded in **resident** code. It writes `$DE00`
  inline; from inside `$8000-$BFFF` the next instruction fetch comes from the
  new bank and execution runs away. Banked code reaching another bank calls a
  resident `__noinline` trampoline (`platform_disk_read()` etc. in
  `src/disk_io.c`, `saveload_save_show()` in `modules/saveload.c`), naming its
  target literally. `tools/check_bank_switches.py` enforces this in `make`.
- Verify Oscar64 output for pointer-walking loops on data that gets persisted:
  `*q++ = *src++` loops were miscompiled at `-Os` (post-loop `q` advance
  dropped). Prefer `copy_bytes()`/`memcpy` plus an explicit advance, and
  self-check encoded sizes. Details and repro: OSCAR64_BUG_POINTER_LOOP_ADVANCE.md.
- Also verify a loop that writes more than one array (or more than one spot
  in the same array) per iteration: `-Os` has merged two per-iteration
  index expressions (`row*3u`/`row*3u+2u`) onto one induction variable,
  silently dropping every write but the first iteration's. One loop per
  destination sequence, not two writes sharing a loop counter. No isolated
  repro found despite three attempts - confirmed only by disassembly plus a
  live VICE memory read. Details: OSCAR64_BUG_LOOP_INDEX_MERGE.md.
- `platform_text_screen_code()` (src/platform.c) only makes sense applied to
  a compile-time string-literal byte - `platform.h`'s `#pragma charmap`
  pre-encodes those (lowercase-in-source -> 65-90, uppercase-in-source ->
  193-218) for the rest of any file that includes it, and the function
  reverses that specific encoding. A *raw* byte (a keyboard keypress, or a
  disk-stored value written from one) is a different domain: GETIN's
  unshifted-letter uppercase (65-90) already *is* this charset's uppercase
  glyph code, so running it through the same function renders the lowercase
  glyphs instead (confirmed against assets/charset.cchr - not an oscar64
  bug, a two-domains-one-function mixup). Poke a raw byte straight to screen
  RAM; see modules/script.c's read_keyword() and modules/saveload.c/
  modules/saveload_save.c's `put_char_raw()`.
- Resident code/state and stacks must stay outside `$8000-$BFFF`;
  main ends before the `$7E00` environment window.
- Keep charset, screen and sprite data in high VIC-bank-3 RAM beneath KERNAL.
  CPU reads require the resident RAM gate; stores already reach RAM.
- Explicitly initialize non-main RAM regions. CRT startup does not populate them.
- Environments begin with four native pointers in an entry section, not opcodes.
  Keep that section before all constants/code.
- Use assembly expressions/`accu` for assembly return values, and the correct
  Oscar64 interrupt qualifier for each actual entry path.
