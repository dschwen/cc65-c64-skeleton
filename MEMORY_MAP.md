# Runtime memory map

This is the authoritative overview of the game's C64 RAM ownership. Fixed
linker ranges come from `cfg/myc64.cfg`; actual ends and free tails below come
from `build/game.map`. Recheck the map after every resident-code or fixed-buffer
change.

## CPU banking during gameplay

The normal gameplay mapping uses `$01` low bits `%101` (normally `$35`):

| CPU range | Gameplay CPU view | Underlying RAM owner |
|---|---|---|
| `$A000-$BFFF` | RAM; BASIC ROM is out | overlays, work buffers, BSS, stack, journal |
| `$D000-$DFFF` | VIC/SID/CIA/Color RAM I/O | object types 64-127 underneath I/O |
| `$E000-$FFFF` | RAM; KERNAL ROM is out | object types 128-255 and RAM vectors |

The engine already owns the RAM under both ROMs. `$36` temporarily maps the
KERNAL in while leaving RAM at `$A000-$BFFF`; `$34` exposes RAM under I/O as
well. Code changes only bits 0-2 of `$01` and preserves the cassette-port bits.

RAM under I/O is not generally usable while drawing: selecting it hides the
VIC, SID, CIA, EasyFlash registers, and Color RAM. Accesses to object types
64-127 therefore run with interrupts disabled and stage a 64-byte record in
always-visible RAM before rendering it.

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
| `$0801-$1FAB` | 6059 | startup, low code, C runtime, and initialized data |
| `$1FAC-$1FFF` | 84 | free low-program linker tail |
| `$2000-$27FF` | 2048 | tile charset |
| `$2800-$2FFF` | 2048 | text charset |
| `$3000-$37FF` | 2048 | 256 tile definitions |
| `$3800-$38FF` | 256 | tile properties |
| `$3900-$39F9` | 250 | compact lookup/native code |
| `$39FA-$39FF` | 6 | free linker tail |
| `$3A00-$3BFF` | 512 | eight aligned 64-byte sprite bitmap slots |
| `$3C00-$7FB3` | 17300 | resident platform code/RODATA |
| `$7FB4-$7FFF` | 76 | free resident-code linker tail |
| `$8000-$84E8` | 1257 | current room |
| `$84E9-$855C` | 116 | persistent `GameState` |
| `$855D-$85F7` | 155 | compact native helpers |
| `$85F8-$85FF` | 8 | free linker tail |
| `$8600-$8B43` | 1348 | resident save/world code |
| `$8B44-$8B47` | 4 | free linker tail |
| `$8B48-$98E2` | 3483 | resident game/main/shared room API |
| `$98E3-$98FF` | 29 | free resident tail |
| `$9900-$9CFF` | 1024 | active room-code overlay |
| `$9D00-$9FFF` | 768 | pristine current-room object baseline |
| `$A000-$A4E8` | 1257 | destination-room staging |
| `$A4E9-$ADF8` | 2320 | render work RAM or inventory/story overlay |
| `$ADF9-$B4D8` | 1760 | free work-RAM/overlay tail |
| `$B4D9-$B4FF` | 39 | unallocated gap |
| `$B500-$B80C` | 781 | resident BSS; currently full |
| `$B80D-$B87D` | 113 | independently loaded native/SID helpers |
| `$B87E-$B87F` | 2 | reserved fill before fixed pager entry |
| `$B880-$B9C9` | 330 | bottom-text pager |
| `$B9CA-$B9FC` | 51 | inventory-overlay validator continuation |
| `$B9FD-$B9FF` | 3 | free helper-module tail |
| `$BA00-$BBFF` | 512 | cc65 software stack |
| `$BC00-$BFE7` | 1000 | 200-record sparse room-object journal |
| `$BFE8-$BFFF` | 24 | free journal-region tail |
| `$C000-$CFFF` | 4096 | object types 0-63, normally visible |
| `$D000-$DFFF` | 4096 | object types 64-127 beneath I/O |
| `$E000-$FFF9` | 8186 | object types 128-255 beneath KERNAL |
| `$FFFA-$FFFF` | 6 | direct NMI/reset/IRQ RAM vectors; overlays type 255 reserved bytes |

The `HIGH` tail is the primary margin for modest resident-code growth, but the
portrait API (see below) consumed most of it: only 76 bytes remain there and
29 in `UPPER`. Further resident-code growth will very likely need to move
existing code into a room, inventory/story, or new overlay rather than grow
in place. Recheck `build/game.map` after every change because cc65 can move
code between segments.

## RAM beneath BASIC and KERNAL

### BASIC ROM: `$A000-$BFFF`

All 8 KiB are already exposed as RAM. The render buffers and inventory/story
overlay deliberately share `$A4E9-$B4D8`: the overlay may overwrite render
state because the resident wrapper redraws the room after it returns. Full-tile
lighting reduced active `WORKBSS` to `$A4E9-$ADF8`, leaving a contiguous
1,760-byte tail for future work buffers or overlay growth. Other unallocated
pieces are `$B4D9-$B4FF` (39 bytes), `$B9FD-$B9FF` (3 bytes), and
`$BFE8-$BFFF` (24 bytes).

### KERNAL ROM: `$E000-$FFFF`

All 8 KiB currently back object-type records 128-255. This RAM is visible in
normal gameplay, and the raster IRQ already uses direct RAM vectors and saves
its own A/X/Y registers. It disappears only while a KERNAL routine is mapped
in. Data can safely live here if no code expects to access it during disk or
keyboard KERNAL calls; code placed here must never map the KERNAL in while it
is executing.

Reclaiming this area means changing object-type storage. Current assets define
only types 1-40, but the file/API intentionally reserves all 256 IDs. Practical
options are:

1. Limit resident types to 128 and reclaim 8 KiB, at the cost of losing IDs
   128-255.
2. Split a smaller resident table across `$C000-$CFFF` and part of
   `$E000-$FFFF`, avoiding RAM under I/O and reclaiming the remaining pages.
3. Keep 256 logical IDs but load/cache only types needed by the current room,
   player, and inventory. This retains the platform contract but requires a
   dependency list and a disk/EasyFlash cache loader.

The third option is the most flexible long-term design. Until that cache
exists, `$E000-$FFFF` is not free RAM.

## Sprite allocation and portraits

The VIC uses bank 0, so sprite bitmap data must remain within `$0000-$3FFF`.
The project already reserves all eight aligned bitmap slots:

| Sprite | Bitmap | Pointer | Current role |
|---:|---|---:|---|
| 0 | `$3A00-$3A3F` | `$E8` | 18x18 Look/Take/Use cursor |
| 1 | `$3A40-$3A7F` | `$E9` | portrait quadrant (top-left) |
| 2 | `$3A80-$3ABF` | `$EA` | portrait quadrant (top-right) |
| 3 | `$3AC0-$3AFF` | `$EB` | portrait quadrant (bottom-left) |
| 4 | `$3B00-$3B3F` | `$EC` | portrait quadrant (bottom-right) |
| 5 | `$3B40-$3B7F` | `$ED` | portrait black backdrop, 2x/2x expanded |
| 6 | `$3B80-$3BBF` | `$EE` | available |
| 7 | `$3BC0-$3BFF` | `$EF` | available |

`platform_portrait_show()`/`platform_portrait_hide()` (see `PLATFORM_API.md`)
own sprites 1-5 exclusively; sprites 1-4 are loaded directly from the
256-byte portrait asset (see `tools/asset-editor/README.md`), and sprite 5 is
filled with a constant solid bitmap at show time. Only sprite-0 bits are
otherwise touched elsewhere, so the look cursor and a shown portrait can be
visible at the same time.

Five portrait sprites plus the cursor are feasible without raster
multiplexing and require no additional bitmap RAM. Each portrait is 24x21
pixels in hires mode or 12x21 logical pixels in multicolor mode. Multicolor
sprites share colors in `$D025/$D026` and retain one per-sprite color.

Portrait code must preserve other sprites' bits in `$D010`, `$D015`, `$D017`,
`$D01B`, `$D01C`, and `$D01D`, as the cursor API already does for sprite 0.
Write or clear a bitmap before enabling its `$D015` bit to avoid visible
partial updates. Six active sprites increase VIC DMA load, but static portraits
are well within the hardware design; verify raster timing if they overlap the
bottom charset-switch interrupt. Inventory currently hides only sprite 0, so a
portrait manager should explicitly hide/restore sprites 1-5 when entering a
full-screen inventory view if portraits should not remain visible there.

The eight pointer bytes at `$07F8-$07FF` are outside the 1000-byte screen clear,
and the raster IRQ changes only the charset half of `$D018`. Portrait pointers
therefore remain valid across map/text charset switching.

## Checking the map

Build and inspect the linker segment list with:

```bash
make
sed -n '/Segment list:/,/Exports list by name:/p' build/game.map
sed -n '/Segment list:/,/Exports list by name:/p' build/text.map
sed -n '/Segment list:/,/Exports list by name:/p' build/inventory.map
```

Room overlays have individual maps under `build/rooms/room-XX.map`.
