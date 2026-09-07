# Target memory map — stage 1 of the banked-memory rework

Goal of stage 1: get every byte the **CPU must read** out of `$8000-$BFFF`, and
move the VIC's data (screen, charsets, sprites) into it. After stage 1 the game
still runs exactly as today — cartridge unmapped during gameplay
(`CPU_MAP_GAME`, `EASYFLASH_OFF`), copy-based overlays intact — but the whole
`$8000-$BFFF` window is then free to become persistent cartridge ROM in stage 2,
at which point far calls and bank-native rooms become possible.

This works only because the cartridge is *not* mapped during normal gameplay
today (`src/banking.s`: `CPU_MAP_GAME = $05`, EasyFlash control `EASYFLASH_OFF`),
so stage 1 is independent of the far-call machinery.

## VIC bank 2 layout (`$8000-$BFFF`)

CIA2 `$DD00` bits 0-1 = `%01` selects bank 2. All VIC pointers below are
**bank-relative** — that is the easy thing to get wrong, especially sprite
pointers.

```
$8000-$83F7  screen matrix          VM=%0000   (D018 high nibble 0)
$83F8-$83FF  sprite pointers        (last 8 bytes of the matrix)
$8400-$85FF  sprite data            8 blocks of 64 bytes, block index 16-23
$8600-$8FFF  free — CPU RAM, VIC-visible but unused
$9000-$9FFF  chargen shadow: VIC sees character ROM here, NOT the RAM.
             CPU can use it freely; nothing the VIC reads may live here.
$A000-$A7FF  tile charset           CB=%100 -> D018 = $08
$A800-$AFFF  text charset           CB=%101 -> D018 = $0A
$B000-$BFFF  free — CPU RAM
```

`TILE_MEMPTR` `$18 -> $08` and `TEXT_MEMPTR` `$1a -> $0a` (`src/irq.s`). Only the
low nibble changes, so the mid-screen split logic is untouched. Colour RAM stays
at `$D800` — it is not part of the VIC bank and never moves.

## What must evacuate, and where it goes

Freed below `$8000` once the VIC data moves up: charsets `$2000-$2FFF` (4096),
sprite storage `$3A00-$3BBF` (448), screen `$0400-$07E7` (1000) = **~5.4KB**.

| Segment | Now | Size | Goes to | Why it must move |
|---|---|---|---|---|
| ROOMBSS | `$8000-$83E8` | 1001 | `$2000` region | screen matrix wants `$8000` |
| GAMESTATE | `$84E9-$855C` | 116 | `$2000` region | inside screen/sprite area |
| STATEEXT | `$855D-$85FF` | 163 | `$2000` region | inside sprite area |
| overlay window | `$A4E9-$B4FF` | 4119 | `$E400-$F5FF` | collides with text charset |
| ROOMSTAGE | `$A000-$A3E9` | 1002 | `$2000` region | collides with tile charset |

`SAVECODE` (`$8600-$8B43`) and `UPPERCODE`/`UPPERRODATA` (`$8B48-$9855`) can stay
put in stage 1 — they sit in `$8600-$8FFF` and `$9000-$9FFF`, which the VIC does
not read. `BSS` (`$B500-$B80A`), the text helpers (`$B80D-$B9FF`), the cc65
software stack (`$BA00-$BBFF`) and `WORLDDELTA` (`$BC00-$BFE7`) also stay in
stage 1; they only need to move in stage 2, when the cartridge becomes
persistent.

## Where the overlay window goes — `$E400` was tried and does NOT work

**Attempted and reverted (kept as `git stash` "overlay window -> E400 WIP").**
Relocating the window to `$E400` links cleanly: all six overlays relink, the
finalize tools validate, room code still stages correctly through the new window
to `$9900`, object types still load, and boot reaches the main loop. But
**overlay validation then rejects every overlay at the checksum step**, so
nothing ever runs. Confirmed as a genuine regression by testing the same
inventory keypress on the previous commit, where `platform_overlay_run_native`
is reached and `_platform_overlay_validate_invalid` is not.

What is known: the fetched payload is byte-identical in RAM to the built
overlay file (all 1166 bytes of `IV`), the file's own stored checksum is
internally consistent, and validation gets as far as
`_platform_overlay_validate_bss_bounds` — so the size, magic, ABI and
entry-target checks all pass. Only the checksum loop, which walks the payload
via `(ptr1),y` from `$E410`, disagrees.

The likely reason is that `$E000-$FFFF` is a bad neighbourhood for anything
the CPU must *read* around a bank call: the ROMH copy path uses
`CPU_MAP_CART_16K` (`$07`, HIRAM=1), which maps the KERNAL straight over the
destination window while copying into it. Writes pass through to RAM, which is
why the bytes land, but any read in that window under that map returns ROM.
Whatever the exact mechanism, under-KERNAL RAM is the wrong home for executable
overlays, and this should not be retried without first proving CPU reads at the
new base under every map the load path passes through.

Note also that this whole move is **throwaway**: once far calls land and
overlays become banked code executed in place, the window disappears entirely.
Freeing `$A000-$AFFF` for the charsets may be better achieved by attacking the
problem from the far-call end first, rather than relocating a window that is
about to be deleted.

## Where the overlay window goes (original analysis, superseded above)

The obvious-looking home, `$9000-$9FFF` (VIC-blind, CPU-fine), does **not** work:
the room-code window already lives at `$9900-$9CFF` and the pristine object
baseline at `$9D00-$9FFF`, and those are persistent, not transient — they cannot
share the window. `$9000-$9FFF` is also only 4096 bytes against the largest
overlay's 4107 actually used (`SV`, saveload-save, reaching `$B4F3`).

Destination is instead the **7,416 bytes of already-free RAM at
`$E302-$FFF9`** — give the window `$E400-$F5FF` (4608 bytes), comfortably clear
of the 4107 high-water mark.

It sits under the KERNAL, which the game banks in only briefly inside
`platform_input_poll()` for `SCNKEY`/`GETIN`. That is safe as long as no overlay
is executing across that call and the raster IRQ handler never lives there —
both hold today, but they become real invariants worth asserting.

Space then closes with room to spare: 5,544 bytes freed below `$8000` against
2,282 bytes of `ROOMBSS`/`GAMESTATE`/`STATEEXT`/`ROOMSTAGE` needing a new home,
all of which fits in the vacated charset region at `$2000-$2FFF` alone.

Every overlay is linked to a fixed load address, so this means updating
`cfg/script_overlay.cfg`, `cfg/inventory_overlay.cfg`, `cfg/room_overlay.cfg`
and friends, plus `ROOM_CODE_STAGE`/`OVERLAY` bases in `src/room_runtime.c` and
`src/script_runtime.c`.

## Static assets leave the program blob entirely

`cart/ef_boot.s` copies the PRG into RAM as one **contiguous image**
(`PAYLOAD0_BYTES = $3d00` then `PAYLOAD1_BYTES = $4000` from `PRG_START`,
ending around `$8500`), plus a third bespoke copy for the text module. Charsets,
tile definitions and sprite bitmaps are all `file = %O` inside that image.

Rather than moving them within the image — or adding another bespoke boot-copy
block — they should **stop being part of the blob at all** and become ordinary
fetched resources. The machinery already exists:
`platform_resource_fetch(kind, id, destination, capacity)` copies a checksummed
blob to an **arbitrary destination address**, and `assets/resources/<ID>` raw
blobs already ride on `PLATFORM_RESOURCE_KIND_SCRIPT`. A resource is guaranteed
to fit in one 8 KiB ROML/ROMH half, so a 2 KiB charset never spans a bank switch.

So the charset lands at `$A000` because that is the `destination` argument —
no linker gymnastics, no image stretching, no new boot-copy code path.

### What leaves the blob

| Content | Now | Size | Becomes |
|---|---|---|---|
| tile charset | `$2000-$27FF` | 2048 | resource -> `$A000` (VIC RAM) |
| text charset | `$2800-$2FFF` | 2048 | resource -> `$A800` (VIC RAM) |
| tile defs + properties | `$3000-$38FF` | 2304 | resource -> RAM |
| cursor/portrait sprites | `$3A00-$3B7F` | 384 | resource -> `$8400` (VIC RAM) |
| rain sprite bitmap | `$3B80-$3BBF` | 64 | resource -> `$8400` region |

~6.8 KB out of the contiguous image, and `$2000-$3BBF` stops being
image-constrained at all — which is where the displaced `ROOMBSS`/`GAMESTATE`/
`STATEEXT`/`ROOMSTAGE` go anyway.

**The saving does not land until the VIC moves to bank 2.** A PRG is a load
address followed by contiguous bytes, so marking those areas `file = ""` punches
a hole in the middle of the image and everything after it loads at the wrong
address — `tools/validate_prg_layout.py` catches this immediately. While the VIC
is still in bank 0 the charsets must physically stay in `$0000-$3FFF`, i.e. in
the middle of the file-backed range, so the areas have to keep reserving their
place (`file = %O, fill = yes`) even though they now hold no linked content.

Only once the charsets move to `$A000` (bank 2) do they leave low RAM entirely,
letting the file-backed areas close up and the image actually shrink. So the
asset split and the VIC bank move have to land together to realise the win —
the split alone is correct and verifiable, but byte-neutral.

Staying resident: `STARTUP`/`LOWCODE`/`CODE`/`RODATA`/`DATA` (the fetch
machinery itself must exist before any fetch — no bootstrap paradox, it is all
in `LOWCODE`), `MIDCODE`/`MIDRODATA`, `RAINCODE`, and `HIGHCODE`/`HIGHRODATA`
until code itself starts moving into banks.

### Two things this has to get right

**Boot ordering.** Today the blob copy trivially guarantees the charset is in
place before anything displays. With fetches, `platform_init()` must keep the
screen blanked until the asset fetches complete, then set the VIC bank and
`$D018`, then unblank. More explicit than the current implicit guarantee, but it
is now a real ordering requirement rather than a free one.

**Charset and tile properties are a matched pair.** Tile graphics and
`tile_properties[]` must never be out of step — a mismatch means walls you can
walk through, silently. If tilesets ever become per-region swappable (which this
change makes possible, and is the main content payoff), the two must share a
resource ID and be fetched together as a unit.

## Change points

- `cfg/myc64.cfg` — segment moves above; `CHARSETS`, `TILEMEM`, `SPRITEGAP` and
  `RAINSPRITE` drop out of the image entirely.
- `tools/pack_easyflash.py` — pack those five as resources; shrink
  `PAYLOAD0`/`PAYLOAD1` in `cart/ef_boot.s` to match.
- `src/platform.c` — `platform_init()` gains the asset fetches, in order,
  before the VIC is pointed at them and the screen unblanks.
- `cfg/*_overlay.cfg` — overlay load base `$A4E9 -> $E400`.
- `src/platform.c` — `P_SCREEN_RAM $0400 -> $8000`, `P_SPRITE_POINTERS
  $07f8 -> $83f8`, `P_SPRITE_DATA $3a00 -> $8400`, `P_VIC(0x18) = $18 -> $08`,
  sprite pointer at line ~1704 must become **bank-relative** (`$0400/64 = 16`,
  not `$8400/64`), and CIA2 `$DD00` bank select added at init.
- `src/platform.inc` — `SCREEN_RAM $0400 -> $8000`.
- `src/irq.s` — `TILE_MEMPTR $18 -> $08`, `TEXT_MEMPTR $1a -> $0a`.
- `src/room_runtime.c` — `ROOM_CODE_STAGE` base.

`src/render.s` and `src/text.s` reference `SCREEN_RAM` symbolically and need no
edit.

## Stage 2 (later, after the far-call macro)

Persistent 16K cartridge mapping; evacuate the remaining `$B000-$BFFF` residents;
IRQ must then never *read* `$8000-$BFFF` (charset rotation reads from a resident
source copy instead — writes still pass through to RAM under the cart).
