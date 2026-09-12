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
| `$A000-$AFFF` | RAM | BASIC or ROMH | currently free |
| `$B000-$BFFF` | RAM | BASIC or ROMH | renderer/staging and resident disk driver |
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
| `$0200-$03FF` | KERNAL workspace and vectors; no display data |
| `$0400-$0491` | shared 146-byte `SL`/`SV` workspace; `$0492-$07FF` free of game ownership |
| `$0801-$1FFF` | startup, low resident code/data, and bank-safe `LOWBSS` |
| `$2000-$23E8` | current `PlatformRoom` (1,001 bytes); `$23E9-$23FF` free |
| `$2400-$27F3` | destination room plus SC's 11-byte workspace/control tail; `$27F4-$27FF` free |
| `$2800-$2BE7` | 200-record sparse world-delta journal; `$2BE8-$2BFF` free |
| `$2C00-$2EED` | ordinary resident BSS; `$2EEE-$2EFF` free |
| `$2F00-$2F03` | compact resident state; `$2F04-$2F9F` free |
| `$2FA0-$2FE1` | compact rain/water setup code; `$2FE2-$2FFF` free |
| `$3000-$38FF` | tile definitions and property bytes; save/load temporarily borrows `$3000-$3479`, then restores all 2,304 bytes from EasyFlash before drawing |
| `$3900-$39F6` | compact resident lookup/code; `$39F7-$39FF` free |
| `$3A00-$3BBC` | separately loaded native/SID/text module; `$3BBD-$3BFF` free |
| `$3C00-$7D09` | resident platform code and read-only tables; `$7D0A-$7DFF` free |
| `$7E00-$7FFF` | current room's resident environment module |
| `$8000-$85FF` | file-backed fill needed by the contiguous PRG; hidden by ROML in banked calls |
| `$8600-$8B43` | resident save/world code; `$8B44-$8B47` free |
| `$8B48-$9849` | resident game/main/shared room API; `$984A-$98FF` free |
| `$9900-$9CFF` | active 1 KiB room-code overlay |
| `$9D00-$9FFF` | pristine current-room object baseline |
| `$A000-$AFFF` | free RAM beneath BASIC/ROMH; not usable by in-place ROML code |
| `$B000-$B7BE` | renderer work buffers in normal play; room-code staging and object-type bootstrap use only bounded prefixes/chunks and are followed by reconstruction |
| `$B800-$BA0E` | resident KERNAL save-disk driver; `$BA0F-$BFFF` free |
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
include 40 bytes in `PROGRAM`, 246 in `HIGH`, 182 in `UPPER`, 18 in `BSS`,
4 in `SAVEUPPER`, 30 in `RAINMEM`, 24 in `WORLDDELTA`, 14 in `SAVEWORK`,
65 before the resident disk driver, and 49 after it. `SL` occupies
`$8000-$894D`; `SV` occupies `$8A00-$96EE`, leaving 2,321 bytes before the end
of bank 48 ROML.

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

## Resident, staged, and in-place cartridge code

The system has three relevant execution classes:

1. **Resident code** is ordinary writable RAM code. IRQ entry points, banking
   machinery, frame waits, and anything callable under arbitrary mappings
   must be resident in an always-visible range.
2. **Copied room/environment modules** are independently linked code copied
   to stable RAM: active room hooks at `$9900` and the IRQ-callable environment
   at `$7E00`. `$B000` is only transient staging during room preparation; no
   general-purpose executable service runs there anymore.
3. **In-place cartridge services** are SL at `$8000`, SV at `$8A00`, and
   Inventory in bank 48; Room Helpers at `$84C0`, SC at `$8800`, Look Helpers
   at `$9300`, and Type Info at `$9F80` in bank 47. Their
   code is immutable and any read from `$8000-$BFFF` sees ROM/BASIC rather than
   RAM. Their stack and persistent state therefore live outside that window,
   and every imported callee/data address is checked against the effective
   mapping. RH's parameter/result block is low resident DATA at `$1F40-$1F46`;
   LH borrows 595 bytes of otherwise-idle room staging RAM for hit/count/text
   scratch, avoiding a second mapping-sensitive collision pass. SC uses the
   same staging room as its resource window and owns 10 bytes at `$27E9-$27F2`
   for window metadata. SC's six unsafe engine actions cross a shared RAM-call
   gate: the far-call trampoline selects cartridge-off `$35/$04`, allows nested
   EasyFlash fetches to restore that state, then restores SC's `$37/$06`.

SL/SV share a resident 146-byte workspace at `$0400`, and borrow `$3000` for
the maximum 1,146-byte record. That buffer aliases immutable tile source data,
so the resident wrapper restores the complete tile resource on every exit
before a draw or room transition. Their KERNAL calls cross explicit low-RAM
gates to the one resident driver at `$B800`. Those gates select cartridge-off
`$01=$36/$DE02=$04`; the driver suspends the raster source for each complete
IEC transaction and the far-call trampoline restores bank 48 ROML afterward.

The in-place model removes copy latency and saves overlay RAM only when the
entire service's mutable-data closure is accessible. It is not a general
replacement for overlays. A C routine can silently touch globals, cc65 helper
routines, string literals, zero-page temporaries, and the software stack;
moving only its code to ROM is unsafe. It also changes VIC visibility: bank 2
was rejected in testing because 16 KiB ROMH replaced the VIC's charset even
though the CPU-side RAM contents were correct. Bank 3 solves VIC visibility,
but its data remains CPU-invisible whenever KERNAL is mapped.

There is no longer a generic copied-overlay ABI, run-time overlay header, or
checksum validator. Any new in-place service still requires a map-derived
dependency closure, explicit value-oriented inputs/outputs, no self-modifying
code, and PAL/NTSC tests while its cartridge half is actually selected.
Coarse service calls remain preferable to fine-grained bank switching.

## Build-time and emulator checks

`make cartridge` validates resident PRG holes, fixed module addresses,
read-only banked segments/import visibility, generated placements, and the CRT.
Use the linker maps as the source of truth:

```sh
make cartridge
sed -n '/Segment list:/,/Exports list by name:/p' build/game.map
sed -n '/Segment list:/,/Exports list by name:/p' build/text.map
sed -n '/Segment list:/,/Exports list by name:/p' build/inventory.map
```

VICE regressions must inspect structured state as well as screenshots: `$01`,
`$DD00`, `$D018`, `$DE00/$DE02`, the frame counter, SL/SV entry and disk-gate
maps, restored tile bytes at `$3000`, and relevant screen/charset bytes. A
clean screenshot alone cannot prove that a nested far call restored its bank
or that an IRQ ran safely.
