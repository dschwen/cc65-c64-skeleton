# Runtime memory map

This is the authoritative overview of the game's C64 RAM ownership. Fixed
linker ranges come from `cfg/myc64.cfg`; actual ends and free tails below come
from `build/game.map`. Recheck the map after every resident-code or fixed-buffer
change.

## CPU banking during gameplay

The normal gameplay mapping uses `$01` low bits `%101` (normally `$35`):

| CPU range | Gameplay CPU view | Underlying RAM owner |
|---|---|---|
| `$A000-$BFFF` | RAM; BASIC ROM is out | overlays, work buffers, BSS, journal |
| `$D000-$DFFF` | VIC/SID/CIA/Color RAM I/O | hot object-type records for types 106-222 underneath I/O |
| `$E000-$FFFF` | RAM; KERNAL ROM is out | hot object-type records for types 223-255 and RAM vectors |

The engine already owns the RAM under both ROMs. `$36` temporarily maps the
KERNAL in while leaving RAM at `$A000-$BFFF`; `$34` exposes RAM under I/O as
well. Code changes only bits 0-2 of `$01` and preserves the cassette-port bits.

RAM under I/O is not generally usable while drawing: selecting it hides the
VIC, SID, CIA, EasyFlash registers, and Color RAM. Accesses to hot object-type
records for types 106-222 therefore run with interrupts disabled and copy the 35-byte
record to always-visible scratch before rendering it.

## Complete allocation

| Address | Size | Owner / status |
|---|---:|---|
| `$0000-$0001` | 2 | 6510 data-direction and banking ports |
| `$0002-$001F` | 30 | cc65 zero page (26 bytes) plus 4 hot render/lighting scalars (`native_light_source_x/y`, `native_object_columns`, `native_object_row_skip`) |
| `$0020-$00FF` | 224 | KERNAL/cc65 machine state; not declared free |
| `$0100-$01FF` | 256 | hardware stack |
| `$0200-$03FF` | 512 | KERNAL workspace, vectors, and disk-boot relocation |
| `$0400-$07E7` | 1000 | screen matrix |
| `$07E8-$07F7` | 16 | unused screen-block tail; available only for tiny fixed state |
| `$07F8-$07FF` | 8 | sprite pointers 0-7 |
| `$0801-$1FFF` | 6143 | startup, low code, C runtime, initialized data, and `LOWBSS` (state banked code must be able to read - see below) |
| `$2000-$27FF` | 2048 | tile charset - reserved only; fetched at boot as an asset resource, not linked in |
| `$2800-$2FFF` | 2048 | text charset - reserved only; fetched at boot |
| `$3000-$37FF` | 2048 | 256 tile definitions - reserved only; fetched at boot |
| `$3800-$38FF` | 256 | tile properties - fetched with the tile definitions as one blob, so the two can never drift apart |
| `$3900-$39FF` | 256 | compact lookup/native code |
| `$3A00-$3B7F` | 384 | cursor and portrait sprite bitmap slots 0-5 |
| `$3B80-$3BBF` | 64 | fixed rain streak bitmap shared by hardware sprites 1-7 |
| `$3BC0-$3BFF` | 64 | compact rain-advance/water-animation code in sprite slot 7 storage |
| `$3C00-$7FFF` | 17408 | resident platform code/RODATA |
| `$8000-$84E8` | 1257 | current room |
| `$84E9-$855C` | 116 | reserve, formerly `GameState` (see `$C100`); still reserved because a hole here would break the contiguous load image |
| `$855D-$85EE` | 146 | compact native helpers |
| `$85EF-$85FF` | 17 | free `STATEEXT` tail |
| `$8600-$8B43` | 1348 | resident save/world code |
| `$8B44-$8B47` | 4 | free linker tail |
| `$8B48-$988F` | 3400 | resident game/main/shared room API |
| `$9890-$98FF` | 112 | free resident tail |
| `$9900-$9CFF` | 1024 | active room-code overlay |
| `$9D00-$9FFF` | 768 | pristine current-room object baseline |
| `$A000-$A4E8` | 1257 | destination-room or save-record/index staging (includes `script_resource_kind`, borrowing 1 byte of this region's own 256-byte slack - see `PLATFORM_API.md`) |
| `$A4E9-$ACA7` | 1983 | render work RAM or a remaining loaded overlay |
| `$ACA8-$B4FF` | 2136 | free work-RAM/overlay tail |
| `$B500-$B7DD` | 734 | resident BSS |
| `$B7DE-$B80C` | 47 | free BSS tail after moving raster state to `LOWBSS` |
| `$B80D-$B87D` | 113 | independently loaded native/SID helpers |
| `$B87E-$B87F` | 2 | reserved fill before fixed pager entry |
| `$B880-$B9C9` | 330 | bottom-text pager |
| `$B9CA-$B9FC` | 51 | generic loaded-overlay validator continuation |
| `$B9FD-$B9FF` | 3 | free helper-module tail |
| `$BA00-$BBFF` | 512 | free (the software stack moved to `$C000`) |
| `$BC00-$BFE7` | 1000 | 200-record sparse room-object journal |
| `$BFE8-$BFFF` | 24 | free journal-region tail |
| `$C000-$C0FF` | 256 | cc65 software stack |
| `$C100-$C173` | 116 | persistent `GameState` |
| `$C174-$C178` | 5 | mutable state for the in-place Inventory service |
| `$C179-$C17F` | 7 | free always-visible RAM |
| `$C180-$CFFD` | 3710 | hot object-type records, types 0-105, normally visible |
| `$CFFE-$CFFF` | 2 | free linker tail |
| `$D000-$DFFE` | 4095 | hot object-type records, types 106-222, beneath I/O |
| `$DFFF` | 1 | free linker tail |
| `$E000-$E482` | 1155 | hot object-type records, types 223-255, beneath KERNAL |
| `$E483-$FFF9` | 7031 | free; reclaimed from the pre-split 64-byte object-type table |
| `$FFFA-$FFFF` | 6 | direct NMI/reset/IRQ RAM vectors |

`PROGRAM` has 53 bytes of margin and `HIGH` has 33; `RAINCODE` (1 byte) and
`MIDCODE` (6 bytes) are tight. The nearby tails are 112 bytes in `UPPER`, 47 in
`BSS`, 17 in `STATEEXT`, and 4 in `SAVECODE`. `PROGRAM` became the tightest
ordinary region when the raster IRQ's frame/rain state moved into `LOWBSS` so
it remains visible during cartridge execution; moving the generated far-call
stubs to `HIGHCODE` recovered low space but made `HIGH` the tighter of those
two resident regions. Larger additions need
relocation or another fixed region. Recheck `build/game.map` after every change
because cc65 can move code between segments.

## RAM beneath BASIC and KERNAL

### BASIC ROM: `$A000-$BFFF`

All 8 KiB are already exposed as RAM. The render buffers and remaining loaded
overlays deliberately share `$A4E9-$B4FF`: an overlay may overwrite render
state because its resident wrapper redraws the room after it returns. Inventory
now executes directly from bank 48 ROMH and no longer consumes this window. Active
`WORKBSS` currently ends at `$ACA7`, leaving a contiguous 2,136-byte tail for
future work buffers or overlay growth. `$A000-$A4E8`
stages either a destination room or a complete save record; those uses never
overlap. `BSS` (`$B500-$B7DD`) has a 47-byte tail after moving the complete
raster frame/rain state to `LOWBSS`. Other
unallocated pieces are `$B9FD-$B9FF` (3 bytes) and `$BFE8-$BFFF` (24 bytes).

### KERNAL ROM: `$E000-$FFFF`

Only `$E000-$E482` (1,155 bytes) backs object-type records now (types
223-255's hot fields); `$E483-$FFF9` is genuinely free. This RAM is visible in normal
gameplay, and the raster IRQ already uses direct RAM vectors and saves its own
A/X/Y registers. It disappears only while a KERNAL routine is mapped in. Data
can safely live here if no code expects to access it during disk or keyboard
KERNAL calls; code placed here must never map the KERNAL in while it is
executing.

Object-type storage was split (see `PLATFORM_API.md` and
`EASYFLASH_CARTRIDGE.md`) into a 35-byte hot record (dimensions, hotspot,
chars, colors, light) kept resident for all 256 IDs at a fixed 35-byte
stride across three zones starting at `$C180`, `$D000`, and `$E000`, and a
15-byte cold record (name, flags) that
stays on EasyFlash bank 47 and is fetched on demand via
`platform_object_type_info_get()`. This reclaimed 7,416 bytes versus the
previous 64-byte-per-record resident table without dropping any of the 256
logical IDs or requiring a per-room dependency list.

## Sprite allocation and portraits

The VIC uses bank 0, so sprite bitmap data must remain within `$0000-$3FFF`.
The project already reserves all eight aligned bitmap slots:

| Sprite | Bitmap | Pointer | Current role |
|---:|---|---:|---|
| 0 | `$3A00-$3A3F` | `$E8` | 18x18 Look/Take/Use cursor |
| 1 | `$3A40-$3A7F` or `$3B80-$3BBF` | `$E9` or `$EE` | portrait quadrant (top-left), or a rain streak |
| 2 | `$3A80-$3ABF` or `$3B80-$3BBF` | `$EA` or `$EE` | portrait quadrant (top-right), or a rain streak |
| 3 | `$3AC0-$3AFF` or `$3B80-$3BBF` | `$EB` or `$EE` | portrait quadrant (bottom-left), or a rain streak |
| 4 | `$3B00-$3B3F` or `$3B80-$3BBF` | `$EC` or `$EE` | portrait quadrant (bottom-right), or a rain streak |
| 5 | `$3B40-$3B7F` or `$3B80-$3BBF` | `$ED` or `$EE` | portrait black backdrop, or a rain streak |
| 6 | `$3B80-$3BBF` | `$EE` | rain streak |
| 7 | `$3B80-$3BBF` | `$EE` | rain streak |

`platform_portrait_show()`/`platform_portrait_hide()` (see `PLATFORM_API.md`)
own sprites 1-5, and rain (see below) owns sprites 1-7; the two are mutually
exclusive; `platform_portrait_show()`/`hide()` pause and resume rain around a
shown portrait. Sprites 1-4 are loaded directly from the 256-byte portrait
asset (see `tools/asset-editor/README.md`) when a portrait is shown, and
sprite 5 is filled with a constant solid bitmap. Only sprite-0 bits are
otherwise touched elsewhere, so the look cursor and a shown portrait (or rain)
can be visible at the same time.

Each portrait is 24x21 pixels in hires mode or 12x21 logical pixels in
multicolor mode. Multicolor sprites share colors in `$D025/$D026` and retain
one per-sprite color.

Portrait code must preserve other sprites' bits in `$D010`, `$D015`, `$D017`,
`$D01B`, `$D01C`, and `$D01D`, as the cursor API already does for sprite 0.
Write or clear a bitmap before enabling its `$D015` bit to avoid visible
partial updates. Rain owns bits 1-7 and preserves bit 0 (cursor); because rain
is always disabled before a portrait shows, portrait code touching the same
bits never collides with it. Inventory currently hides only sprite 0, so a
portrait manager should explicitly hide/restore sprites 1-5 when entering a
full-screen inventory view if portraits should not remain visible there.

All seven rain sprites point at the same static streak bitmap in slot 6
(`$3B80-$3BBF`); each sprite is simply positioned independently every frame,
so no raster multiplexing is needed. Slot 7's 64 bytes hold the compact
rain-advance/water-animation code instead of a bitmap; this is safe because
nothing ever points a sprite at slot 7. The eight pointer bytes at
`$07F8-$07FF` are outside the 1000-byte screen clear, and the raster IRQ
changes only the charset half of `$D018`. Portrait/rain pointers therefore
remain valid across map/text charset switching.


## Banked code and what it may touch

Code can now run directly from an EasyFlash bank instead of being copied into
RAM first (see `PLATFORM_API.md`'s "Banked code"). ROML always occupies
`$8000-$9FFF`. In 16 KiB mode ROMH occupies `$A000-$BFFF`; in 8 KiB mode the
required `$01=$37` CPU map puts BASIC ROM there instead. Thus every byte in
this table between `$8000` and `$BFFF` is unreadable as underlying RAM during
either kind of in-place call. Writes still reach the RAM underneath.

A banked routine may therefore touch only:

| Region | Why it is safe |
|---|---|
| `$0000-$7FFF` | never covered by the cartridge |
| `$C000-$CFFF` | the only RAM above `$8000` that no memory map ever covers |
| `$D000-$DFFF`, `$E000-$FFFF` | reachable, but only under the usual I/O/KERNAL banking dance |

EasyFlash control and CPU mapping are independent. `$DE02=$06` means 8 KiB
cartridge mode, but the corresponding CPU port is `$37`, not `$36`; `$36`
disables ROML along with BASIC. The generated far-call descriptor carries both
values to prevent that easy-to-miss substitution.

This is why the software stack and `GameState` were moved to `$C000` and
`$C100`: cc65-generated code touches its stack constantly, so no C function
could have run banked while the stack sat at `$BA00`. `LOWBSS` (inside
`PROGRAM`) exists for the same reason - it holds state that banked code, or the
bank machinery it calls, has to be able to read. The complete raster IRQ's
mutable frame/rain state also lives there so it remains usable with ROML/ROMH
selected.

The same rule applies to *code*: a banked routine may only call resident code
that lives outside the window. `UPPERCODE` (`$8B48`) is inside it, so anything
there is unreachable from a bank - which is why `_raster_irq_resync` was moved
out of it and into `HIGHCODE`.

It also may not modify its own instructions. Cartridge-window writes update
the underlying RAM, not ROM, so a later fetch still sees the original opcode
or operand. This is different from ordinary copied overlays, which are writable
RAM code. Inventory's former self-patched draw address was converted to a
zero-page indirect pointer before moving it in place.

Cost: each banked call pays a fixed map/register transition. Interrupts are
masked only for that transition and restored while the banked operation runs,
so long calls no longer freeze the raster split. Bank at operation rather than
inner-loop granularity to avoid repeated transition overhead.

## Checking the map

Build and inspect the linker segment list with:

```bash
make
sed -n '/Segment list:/,/Exports list by name:/p' build/game.map
sed -n '/Segment list:/,/Exports list by name:/p' build/text.map
sed -n '/Segment list:/,/Exports list by name:/p' build/inventory.map
```

Room overlays have individual maps under `build/rooms/room-XX.map`.
