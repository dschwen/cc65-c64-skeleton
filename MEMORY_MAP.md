# Runtime memory map

This is the authoritative overview of the game's C64 RAM ownership. Fixed
ranges come from `cfg/myc64.cfg`; occupied ends come from `build/game.map` and
`build/text.map`. Recheck both maps after every resident-code or fixed-buffer
change.

## CPU and VIC banking

Normal gameplay uses `$01=$35`: BASIC and KERNAL are out, I/O is visible.
EasyFlash calls temporarily use `$01=$37` and either 8 KiB (`$DE02=$06`) or
16 KiB (`$DE02=$07`) cartridge mode. This makes ROML visible at
`$8000-$9FFF`; 16 KiB mode also exposes ROMH at `$A000-$BFFF`, while 8 KiB
mode exposes BASIC ROM there. The underlying RAM in the whole
`$8000-$BFFF` range is therefore unreadable during every in-place cartridge
call, although writes still reach RAM.

The VIC-II now uses bank 3 (`$C000-$FFFF`, CIA2 `$DD00` bits 0-1 = `%00`).
Unlike the CPU, the VIC sees RAM beneath KERNAL at `$E000-$FFFF`. This keeps
the display visible while the CPU runs cartridge code, and avoids the fatal
bank-2 arrangement where ROMH replaced the VIC's charset during a 16 KiB
call. CPU code must still not read screen, charset, or sprite RAM while
KERNAL is mapped.

| CPU range | Normal `$35` view | Banked-call `$37` view | Underlying RAM owner |
|---|---|---|---|
| `$8000-$9FFF` | RAM | EasyFlash ROML | fill, resident code, room code/baseline |
| `$A000-$AFFF` | RAM | BASIC or ROMH | save/index scratch |
| `$B000-$BFFF` | RAM | BASIC or ROMH | work RAM / copied-overlay window |
| `$D000-$DFFF` | I/O | I/O | object-type records beneath I/O |
| `$E000-$FFFF` | RAM | KERNAL | object types, VIC data, vectors beneath KERNAL |

Code changes only `$01` bits 0-2 and preserves the cassette-port bits.
`$34` is used briefly to expose RAM beneath I/O. Such accesses mask
interrupts because I/O, including VIC and EasyFlash registers, is hidden.

## Complete allocation

| Address | Occupied / reserved owner |
|---|---|
| `$0000-$0001` | 6510 direction and banking ports |
| `$0002-$001F` | cc65 zero page plus hot render/lighting scalars |
| `$0020-$00FF` | KERNAL/cc65 machine state; not free |
| `$0100-$01FF` | hardware stack |
| `$0200-$07FF` | KERNAL workspace, vectors, disk-loader relocation; no display data |
| `$0801-$1FFF` | startup, low resident code/data, and bank-safe `LOWBSS` |
| `$2000-$23E8` | current `PlatformRoom` (1,001 bytes); `$23E9-$23FF` free |
| `$2400-$27E9` | destination room plus one staging byte; `$27EA-$27FF` free |
| `$2800-$2BE7` | 200-record sparse world-delta journal; `$2BE8-$2BFF` free |
| `$2C00-$2EDD` | ordinary resident BSS; `$2EDE-$2EFF` free |
| `$2F00-$2F8F` | compact resident helpers/state; `$2F90-$2F9F` free |
| `$2FA0-$2FE1` | compact rain/water setup code; `$2FE2-$2FFF` free |
| `$3000-$38FF` | tile definitions and property bytes |
| `$3900-$39F9` | compact resident lookup/code; `$39FA-$39FF` free |
| `$3A00-$3BEF` | separately loaded native/SID/text/overlay-validator module; `$3BF0-$3BFF` free |
| `$3C00-$7DBC` | resident platform code and read-only tables; `$7DBD-$7DFF` free |
| `$7E00-$7FFF` | current room's resident environment module |
| `$8000-$85FF` | file-backed fill needed by the contiguous PRG; hidden by ROML in banked calls |
| `$8600-$8B43` | resident save/world code; `$8B44-$8B47` free |
| `$8B48-$98DD` | resident game/main/shared room API; `$98DE-$98FF` free |
| `$9900-$9CFF` | active 1 KiB room-code overlay |
| `$9D00-$9FFF` | pristine current-room object baseline |
| `$A000-$A479` | maximum 1,146-byte save record, or 146-byte index; `$A47A-$AFFF` free |
| `$B000-$B7BE` | renderer work buffers in normal play |
| `$B000-$BFFF` | temporally exclusive copied-overlay / room-code / type staging window |
| `$C000-$C0FF` | cc65 software stack |
| `$C100-$C178` | `GameState` and in-place Inventory service state |
| `$C180-$CFFD` | hot object-type records 0-105; `$CFFE-$CFFF` free |
| `$D000-$DFFE` | hot object-type records 106-222 beneath I/O; `$DFFF` free |
| `$E000-$E482` | hot object-type records 223-255 beneath KERNAL |
| `$E483-$E7FF` | free RAM beneath KERNAL |
| `$E800-$EFFF` | tile charset (VIC bank 3) |
| `$F000-$F7FF` | text charset (VIC bank 3) |
| `$F800-$FBE7` | 40x25 screen matrix |
| `$FBE8-$FBF7` | unused screen-block tail |
| `$FBF8-$FBFF` | eight sprite pointers |
| `$FC00-$FD7F` | cursor and portrait sprite slots 0-5 |
| `$FD80-$FDBF` | shared procedural rain bitmap (slot 6) |
| `$FDC0-$FDFF` | unused sprite slot 7 storage |
| `$FE00-$FFF9` | free RAM beneath KERNAL |
| `$FFFA-$FFFF` | direct NMI/reset/IRQ RAM vectors |

The tightest fixed regions are deliberate and build-checked. Current margins
include 44 bytes in `PROGRAM`, 67 in `HIGH`, 34 in `UPPER`, 34 in `BSS`, 30
in `RAINMEM`, 24 in `WORLDDELTA`, and 16 in both the text-module range and
`STATEEXT`. The save-detail overlay has a separately enforced 4,064-byte
ceiling and currently uses 4,001 bytes including BSS, leaving 95 bytes in the
physical 4 KiB page.

## Display and interrupt ownership

`$D018=$EA` selects screen `$F800` and tile charset `$E800`; `$D018=$EC`
selects the text charset `$F000`. Bit 0 is unused and may read back set, so
debuggers commonly show `$EB/$ED`. The raster handler switches to text above
rows 23-24 and restores the tile charset at the next frame. Row 22 remains a
spacer.

Sprite pointers are relative to VIC bank 3. Sprite 0 uses `$FC00`; portrait
sprites 1-5 use `$FC40-$FD40`; rain sprites 1-7 all point to the single bitmap
at `$FD80`. The rain bitmap is initialized procedurally, eliminating a static
64-byte asset. The cursor and rain/portrait owners preserve one another's VIC
bit fields, and portrait and rain modes are mutually exclusive.

The IRQ entry at `$FFFE/$FFFF` saves A/X/Y, acknowledges `$D019`, and never
uses the cc65 software stack. A second `$0314` entry is valid while KERNAL is
mapped for disk I/O. Raster phase, frame counters, rain positions, and an
eight-byte water-glyph shadow live in `LOWBSS` below `$8000`, so interrupts
remain safe while ROML/ROMH or KERNAL hides upper RAM. Water animation rotates
the low shadow and writes it to `$E870`; it never reads the charset under a
KERNAL mapping. Long copies suspend the VIC source explicitly and resume with
a raster resynchronization; far-call transitions mask interrupts only while
changing `$01` and EasyFlash registers.

The custom IRQ does not chain through KERNAL and disables CIA1 interrupts, so
the KERNAL jiffy clock and its keyboard scan do not run. Foreground code calls
`SCNKEY`/`GETIN` once per frame. Any future IRQ feature must obey all of these
rules: no C calls, no software-stack use, no cartridge-window reads, no
KERNAL-hidden reads, and bounded work before the row-22 badline.

## Resident, copied-overlay, and in-place cartridge code

The system now has three execution classes:

1. **Resident code** is ordinary writable RAM code. IRQ entry points, banking
   machinery, frame waits, and anything callable under arbitrary mappings
   must be resident in an always-visible range.
2. **Copied overlays** (`LH`, `SC`, `SL`, `SV`) are copied from cartridge
   into `$B000-$BFFF`, validated, run synchronously, and discarded. They may
   use writable code and normal RAM semantics, but cannot call another owner
   of that same window or read renderer `WORKBSS` while they occupy it.
   Resident wrappers stage arguments/results and repair or redraw after return.
3. **In-place cartridge services** are Inventory in bank 48 ROMH, Room Helpers
   at `$84C0` in bank 47 ROML, and Type Info at `$9A00` in bank 47 ROML. Their
   code is immutable and any read from `$8000-$BFFF` sees ROM/BASIC rather than
   RAM. Their stack and persistent state therefore live outside that window,
   and every imported callee/data address is checked against the effective
   mapping. RH's parameter/result block is low resident DATA at `$1F40-$1F46`.

The in-place model removes copy latency and saves overlay RAM only when the
entire service's mutable-data closure is accessible. It is not a general
replacement for overlays. A C routine can silently touch globals, cc65 helper
routines, string literals, zero-page temporaries, and the software stack;
moving only its code to ROM is unsafe. It also changes VIC visibility: bank 2
was rejected in testing because 16 KiB ROMH replaced the VIC's charset even
though the CPU-side RAM contents were correct. Bank 3 solves VIC visibility,
but its data remains CPU-invisible whenever KERNAL is mapped.

The four remaining copied overlays are intentionally retained as a compatibility
boundary. Converting one requires a map-derived dependency closure, explicit
value-oriented inputs/outputs, no self-modifying code, PAL and NTSC IRQ tests,
and tests while the relevant cartridge half is actually selected. Coarse
service calls are preferred; bank-switching inner helpers would increase both
latency and the number of interrupt-visible transition points.

## Build-time and emulator checks

`make cartridge` validates the resident PRG holes, fixed module addresses,
overlay headers/checksums/BSS bounds, generated bank placements, and the CRT.
Use the linker maps as the source of truth:

```sh
make cartridge
sed -n '/Segment list:/,/Exports list by name:/p' build/game.map
sed -n '/Segment list:/,/Exports list by name:/p' build/text.map
sed -n '/Segment list:/,/Exports list by name:/p' build/inventory.map
```

VICE regressions must inspect structured state as well as screenshots: `$01`,
`$DD00`, `$D018`, `$DE00/$DE02`, the frame counter, overlay headers at `$B000`,
and the relevant screen/charset bytes. A clean screenshot alone cannot prove
that a nested far call restored its bank or that an IRQ ran safely.
