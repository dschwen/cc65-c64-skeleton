# EasyFlash Cartridge Generation

This document records the design decisions and practical lessons behind this
repository's EasyFlash build. It describes the executable and runtime assets,
the relevant C64 and EasyFlash behavior, and the constraints to consider when
expanding the game to use more banks or writable flash.

Primary references:

- [EasyFlash Programmer's Guide](https://skoe.de/easyflash/files/devdocs/EasyFlash-ProgRef.pdf)
- [VICE CRT file format and EasyFlash cartridge description](https://vice-emu.sourceforge.io/vice_17.html)
- [VICE cartconv documentation](https://vice-emu.sourceforge.io/vice_15.html)

## Build and run

Requirements:

- cc65 (`cl65`, `ca65`, and `ld65`)
- VICE `cartconv`
- VICE `x64sc` or another emulator for running the result

Build the cartridge:

```bash
make cartridge
```

Outputs:

| File | Purpose |
|---|---|
| `build/game.prg` | Normal disk/loadable game, built first |
| `build/game-ef-base.bin` | Raw executable banks 0-2 |
| `build/game-ef.bin` | Packed executable and runtime asset banks |
| `build/game-ef.map` | Cartridge bootstrap linker map |
| `build/game.crt` | EasyFlash CRT image for emulators or EasyProg |
| `build/saves.d64` | writable development save disk, created on first cartridge run |

Run it in VICE:

```bash
make run-cartridge
```

This also creates (once) and attaches `build/saves.d64` as writable device 8
storage. Cartridge saves are disk-backed. Override the path with
`SAVE_DISK=/path/to/saves.d64`; deleting the file or running `make clean`
starts with an empty development save disk.

Override the cartridge name stored in the CRT header with:

```bash
make CART_NAME=MYGAME cartridge
```

The relevant source files are:

- `cart/ef_boot.s`: cartridge header, bootstrap, PRG payload, and vectors
- `cfg/easyflash.cfg`: raw bank 0/1 linker layout
- `Makefile`: PRG, raw cartridge, and CRT build pipeline

## Three different layouts

EasyFlash development involves three related but distinct layouts. Treating
them as one address space leads to incorrect vectors and bank ordering.

### Physical flash layout

EasyFlash provides 64 numbered banks. Each bank selects two separate 8 KiB
regions at the same time:

- ROML: the low 8 KiB flash half
- ROMH: the high 8 KiB flash half

This gives 16 KiB per bank and 1 MiB across 64 banks. A write to `$DE00`
selects the bank for both ROML and ROMH.

### C64 CPU windows

In 16 KiB cartridge mode, the selected bank is visible as:

| CPU address | Selected flash region |
|---|---|
| `$8000-$9FFF` | ROML |
| `$A000-$BFFF` | ROMH |

In reset-time Ultimax mode, ROML is still visible at `$8000-$9FFF`, but ROMH
is visible at `$E000-$FFFF`. The same physical final six ROMH bytes therefore
appear to the CPU as `$FFFA-$FFFF` during reset.

EasyFlash has two I/O-1 registers used by this build:

| Register | Use |
|---|---|
| `$DE00` | Select bank 0-63 for ROML and ROMH |
| `$DE02` | Control LED, software mode, `/EXROM`, and `/GAME` |

The bootstrap uses `$07` for software-controlled 16 KiB mode and `$04` to
disable the cartridge. Code that changes `$DE02` must account for the fact
that its own ROM window may disappear immediately.

### CRT container layout

A `.crt` is not a raw memory dump. It has a cartridge header followed by CHIP
packets. Each CHIP packet identifies a bank, CPU load window, and 8 KiB data
payload. For this project's executable image, `cartconv -f` reports the
populated ROML/ROMH chips:

```text
CHIP FLASH #000 $8000 $2000
CHIP FLASH #000 $a000 $2000
CHIP FLASH #001 $8000 $2000
CHIP FLASH #001 $a000 $2000
```

These begin with ROML and ROMH for banks 0-2. `cartconv` omits erased banks, so
the CRT contains 32 KiB of flash data even though EasyFlash has a logical 1 MiB
capacity. Missing banks represent erased `$FF` data.

## Boot paths

The cartridge supports both EasyFlash reset behavior and conventional C64
cartridge autostart.

### Standard `CBM80` header

The KERNAL's normal cartridge check examines `$8004-$8008` for the five-byte
PETSCII signature `CBM80`. A conventional header is:

| Address | Content |
|---|---|
| `$8000-$8001` | Cold-start vector |
| `$8002-$8003` | Warm-start/NMI vector |
| `$8004-$8008` | `$C3,$C2,$CD,$38,$30` (`CBM80`) |

The signature is not stored at the end of ROMH, and it contains five bytes,
not only `CBM`.

### EasyFlash Ultimax reset vectors

EasyFlash starts in Ultimax mode. The CPU obtains its vectors from the end of
ROMH as mapped at `$E000-$FFFF`. In the raw physical `$A000-$BFFF` ROMH
representation used by the linker, the vector bytes belong at:

| Physical linker address | Reset-time CPU address | Use |
|---|---|---|
| `$BFFA-$BFFB` | `$FFFA-$FFFB` | NMI vector |
| `$BFFC-$BFFD` | `$FFFC-$FFFD` | RESET vector |
| `$BFFE-$BFFF` | `$FFFE-$FFFF` | IRQ vector |

`cfg/easyflash.cfg` therefore starts `VECTORS` at `$BFFA`, not `$BFF6`.
All three currently point to `cold_start` at `$8009`, which is in ROML and is
visible in both Ultimax and 16 KiB modes.

## Why the game is copied to RAM

The existing game was designed as a normal cc65 PRG. Its code, initialized
data, custom character sets, tile definitions, BSS, and software stack already
have a working RAM layout. Directly relinking all of that into cartridge ROM
would introduce several new requirements:

- writable `DATA` needs separate load and run addresses plus startup copying;
- `BSS` and the cc65 software stack must remain in RAM;
- `WORKBSS` at `$A000-$BFFF` is underlying RAM and requires EasyFlash ROMH and
  BASIC ROM to be disabled before gameplay accesses it;
- the current room at `$8000-$84E8` is temporarily hidden by ROML, so room
  loads copy into `$A000` staging RAM and commit only after disabling the cart;
- code executing inside a banked window cannot switch away its own bank;
- ROML/ROMH hide RAM or BASIC ROM beneath their CPU windows;
- VIC-visible assets still need a deliberate RAM/VIC-bank strategy.

The current cartridge instead treats the working PRG as its payload:

1. The normal `make` build produces `build/game.prg`.
2. `ef_boot.s` embeds the PRG excluding its two-byte load address, placing
   `$3D00` bytes in bank 0, `$4000` in bank 1, and the remainder in bank 2.
3. The bootstrap initializes CPU port registers `$01` and `$00`.
4. It selects EasyFlash bank 0 and 16 KiB mode.
5. It calls KERNAL `IOINIT`, `RAMTAS`, `RESTOR`, and `CINT`.
6. It copies the bank 0 chunk into RAM beginning at `$0801`.
7. It copies a position-independent second-stage loader to `$C000` and jumps
   there before selecting bank 1 through `$DE00`.
8. The RAM stage copies banks 1 and 2 into contiguous destination RAM.
9. It writes a small disable-and-jump trampoline into free low RAM at `$0400`.
10. The trampoline disables EasyFlash and jumps to cc65 startup at `$080D`.
11. The game initializes its bank-3 screen RAM at `$F800` normally.

The trampoline is necessary because an instruction following `sta $DE02`
could no longer be fetched from ROML after the cartridge is disabled.
The `$C000` stage is necessary for the same reason when `$DE00` selects later
banks: bank 0 ROML, including the first-stage loader, disappears immediately.

This design keeps the PRG and cartridge builds behaviorally aligned and makes
the cartridge a fast, self-contained loader. The cartridge is not banked in
during normal gameplay.

## Current bank layout

`cfg/easyflash.cfg` creates three filled 16 KiB raw banks:

| Range | Segment | Current use |
|---|---|---|
| `$8000-$8008` | `CART_HEADER` | Vectors and `CBM80` signature |
| `$8009-$811B` | `BOOT` | RAM initialization and copy loader |
| `$811C-$81FF` | fill | `$FF` padding |
| bank 0 `$8200-$BEFF` | `PAYLOAD0` | first `$3D00` PRG payload bytes |
| remaining bank 0 space | fill | `$FF` padding |
| `$BFFA-$BFFF` | `VECTORS` | Ultimax NMI, RESET, and IRQ vectors |
| bank 1 `$8000-$BFFF` | `PAYLOAD1` | next `$4000` PRG payload bytes |
| bank 2 `$8000+` | `PAYLOAD2` | remaining PRG payload bytes |
| remainder of bank 2 | fill | `$FF` padding |

The first payload starts at `$8200` to leave room for the bootstrap. The
assembler asserts full `$3D00` and `$4000` chunks, a nonempty third chunk, and
that the remainder fits in bank 2.

The current bootstrap has two intentional hard-coded couplings to the PRG
linker layout:

- PRG load address: `$0801`
- cc65 startup entry: `$080D`

If `cfg/myc64.cfg` changes either value, update `PRG_START` and `CC65_START`
in `cart/ef_boot.s`. Also verify the payload still fits across banks 0-2.

## Linker and `cartconv` pipeline

The cartridge is built in three stages:

```text
C/assembly/assets -> game.prg
game.prg + ef_boot.s + easyflash.cfg -> game-ef-base.bin
game-ef-base.bin + room/type packer -> game-ef.bin
game-ef.bin + cartconv -> game.crt
```

The base linker output is ordered exactly as one EasyFlash bank expects:

```text
8 KiB bank 0 ROML, bank 0 ROMH, bank 1 ROML, then bank 1 ROMH
```

VICE 3.9 `cartconv` rejects a partial EasyFlash input by default because a
fully padded raw EasyFlash image would be 1 MiB. The build uses `-p` to accept
the non-padded 49-bank prefix; `cartconv` omits erased CHIP packets:

```bash
cartconv -p -t easy -i build/game-ef.bin -o build/game.crt -n GAME
```

Do not add a two-byte PRG load address to `game-ef.bin`; it is a raw flash
image. The load address in `game.prg` is removed by `.incbin ..., 2` before
that PRG becomes the cartridge payload.

## Validation

Build and validate the CRT structure:

```bash
make -B cartridge
cartconv -f build/game.crt
cartconv -c build/game.crt
```

Expected metadata includes:

- hardware ID 32 (`EasyFlash`);
- initial `/EXROM` high and `/GAME` low (Ultimax);
- bank 0 `$8000` CHIP packet of `$2000` bytes;
- bank 0 `$A000` CHIP packet of `$2000` bytes.
- bank 1 `$8000` CHIP packet of `$2000` bytes;
- bank 1 `$A000` CHIP packet of `$2000` bytes.

Inspect the raw boot header and Ultimax vectors:

```bash
od -An -tx1 -N32 build/game-ef.bin
od -An -tx1 -j16378 -N6 build/game-ef.bin
```

The first bytes should begin with two `$8009` vectors followed by `CBM80`:

```text
09 80 09 80 c3 c2 cd 38 30
```

The final six bytes should contain three `$8009` vectors:

```text
09 80 09 80 09 80
```

Verify all embedded chunks against the PRG excluding its load address:

```bash
cmp -i 512:2 -n $((0x3d00)) build/game-ef.bin build/game.prg
cmp -i 16384:$((2 + 0x3d00)) -n $((0x4000)) build/game-ef.bin build/game.prg
payload2_size=$(( $(wc -c < build/game.prg) - 2 - 0x7d00 ))
cmp -i 32768:$((2 + 0x7d00)) -n "$payload2_size" build/game-ef.bin build/game.prg
```

The third byte count is computed from the current PRG size and changes with
the game. The linked `PAYLOAD0`/`PAYLOAD1`/`PAYLOAD2` sizes are recorded in
`build/game-ef.map`.

Structural checks do not replace a cold-boot test in VICE and, ideally, on
real EasyFlash hardware. This headless repository runs VICE through
`xvfb-run -a x64sc ...`; bounded cold boots are part of verification.

VICE also warns when an EasyFlash CRT does not contain the optional EasyAPI
signature in bank 0 ROMH. That warning is expected for this read-only game and
does not indicate a malformed boot image. Do not add a fake signature: include
a real, current EasyAPI only when flash writing is implemented.

The reserved EasyAPI location is bank 0 ROMH offset `$1800`, written in the
EasyFlash guide as `00:1:1800`. In this linker's physical ROMH address space it
is `$B800-$BBFF`. A writable-flash image begins there with lowercase `eapi`
bytes and contains the actual flash driver. This read-only build uses that
space for `PAYLOAD0`, so it intentionally has no EasyAPI signature.

## Runtime room loading

The CRT first copies the common PRG to RAM and disables EasyFlash. During
gameplay the storage backend temporarily selects 8 KiB mode to copy rooms and
object types from runtime ROML asset banks.

All game-asset reads (rooms, object types, portraits, the in-place Inventory
service, room code) are EasyFlash-only:

```c
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
```

`platform_room_load()` obtains the 1,257-byte record from its fixed ROML bank
and offset (see the asset packing table below). Disk KERNAL I/O is reserved
for save games (`SAVE_GAME.md`); it is not an alternate asset source. If the
EasyFlash boot marker is absent at startup, `platform_init()` does not attempt
a disk fallback for assets — it keeps the tiny room-00/types-0-1 placeholder
baked into the PRG.

Only the current room is resident for drawing. `platform_room_enter()` stages
and validates the destination, checks the arrival tile, prepares its overlay,
then invokes the store hook while the leaving-room baseline is still current.
The restore hook preflights target capacity before replacing that baseline and
applying destination deltas. The platform then inserts the actor, commits,
updates `platform_player_slot` and `platform_player`, and redraws.
`platform_room_object_transfer()` remains a useful primitive for tools or code
that deliberately keeps two room buffers.

Cartridge room records are immutable base data. Any object that moves between
rooms makes the room state differ from that base. Register store/restore hooks
with `platform_room_state_hooks()` to supply the chosen mutable-state policy:
disk/flash saves, a compact delta journal, or a bounded cache with defined
eviction. Without hooks, reloading base data discards general room mutations;
the platform only suppresses the original baked player spawn so returning to
room `00` does not duplicate the player.

### Asset packing

Reserve EasyFlash banks 0-2 for the executable and begin runtime assets in
bank 3. Prefer generated bank images and an index/manifest over dozens of
overlapping ld65 memory areas.

Runtime reads should normally use EasyFlash 8 KiB mode (`$DE02 = $06`), which
exposes ROML at `$8000-$9FFF` without exposing ROMH over `$A000-$BFFF`. Six
1,257-byte rooms fit in one ROML page:

```text
6 * 1,257 = 7,542 bytes, leaving 650 bytes per 8 KiB page
```

The 256 rooms therefore need 43 ROML pages. Together with 16 KiB of object
types and the three executable banks, this fits in the 64 available EasyFlash
banks when asset data is deliberately placed in ROML pages. A fixed layout can
derive `bank = first_room_bank + room_id / 6`; a generated directory is more
flexible if compression or additional assets are introduced later.

The 8 KiB choice prevents cartridge ROMH from occupying `$A000-$BFFF`, but
ROML still requires the normal CPU mapping `$37`. Mapping `$36` leaves
underlying RAM visible at `$8000-$9FFF`; using it silently copies the current
room or `GameState` instead of cartridge assets. With `$37`, BASIC ROM hides
the C stack and BSS at `$A000-$BFFF`, so runtime ROML copying is a stackless
assembly operation using only the hardware stack, zero page, and low DATA.
Writes through BASIC or KERNAL ROM reach underlying RAM. The `$D000-$DFFF`
object-type middle zone is first staged at `$B000-$BFFE`, then copied with all
RAM mapped so I/O registers are not written. Gameplay mapping `$35` is restored after
the cartridge is disabled. The copy path:

1. disable IRQs and remember the previous interrupt state;
2. write the asset bank to `$DE00`;
3. select 8 KiB mode with `$DE02 = $06`;
4. set CPU mapping `$37` and copy from `$8000-$9FFF` to underlying RAM;
5. disable the cartridge with `$DE02 = $04`;
6. restore the interrupt state;
7. validate the room header before committing it as the current room.

For failure safety, copy into a staging room when RAM permits and replace the
current room only after validation. At minimum, do not update current-room and
player globals until the complete record has been read and validated.

Room-specific code is stored separately in ROMH. Bank 3 ROMH begins with a
256-entry directory; each populated eight-byte entry names a ROMH bank,
offset, size, and checksum. A native assembly routine maps 16 KiB mode and
copies the selected `CXX` overlay to staging without touching the C stack or
BSS, both of which ROMH temporarily hides. ROML room and object-type assets use
the same native copier with an `$8000` source base. See `ROOM_CODE_API.md`.

Active room code does **not** run from `$B000`. `game_room_code_prepare()`
(`src/room_runtime.c`) copies and validates the destination room's code at
`$B000` only as scratch; `game_room_code_activate()` then `memcpy`s the
validated bytes to `$9900` (`ROOM_CODE_BASE`), where the room's
`enter_room()`/`enter_tile()`/`look()`/`use()` handlers actually live and run
for as long as that room stays current. `$B000` is only unsafe for another
overlay to load into during the few instructions inside
`platform_room_enter()` between `prepare()` and `activate()` - that's the
window `platform_room_object_add()` runs in, which is why it stays resident.
It is *not* unsafe for anything called from a room's own already-active
`$9900` code, since by the time that code runs, `activate()` has already
freed `$B000`. (`platform_room_clear()`/`object_transfer()`/
`transition_check()` still have no real callers to verify against, so
they're undecided, not confirmed-unsafe - don't assume "might be called from
room code" alone rules them out.)

The inventory UI and global story-specific item-use code form an independently
linked, execute-in-place service. `tools/pack_easyflash.py` puts its raw linked
bytes at bank 48 ROMH offset zero (`$A000` at run time). Pressing `I` enters it
through the resident far-call stub; no code is copied to `$B000`, so the
render/lighting work buffers survive the UI unchanged. Five mutable UI bytes
live at `$C174-$C178`, immediately after `GameState`. Item names use a nested
far call to the bank-47 type-info service, which fetches its cold record from
bank 46 and then unwinds to bank 48. On return, resident code restores the
charset split and redraws the room. See `STORY_CODE_API.md` for the callable
contract and restrictions.

Room Helpers (`RH`) is the second gameplay service converted from a copied
overlay to this model. Its 452-byte read-only image runs at `$84C0-$8683` in
bank 47 ROML. The resident wrapper stages a seven-byte argument/result block
at `$1F40-$1F46`, then enters through `platform_room_helpers_run_banked()`.
Object-type lookup and dirty-cell discovery deliberately happen before the
far call: type IDs 106-222 require temporarily exposing RAM under I/O, and the
lookup restores gameplay `$01=$35`, which would unmap ROML if performed by RH
itself. Redraw and lighting repair happen after return because their work
arrays occupy `$B000`, hidden by RH's 8 KiB cartridge mapping.

Character portraits (`platform_portrait_show()`, see `PLATFORM_API.md`) use
banks 49-56 in 8 KiB ROML mode, the same mode and fixed
`bank = first_bank + id / per_bank` formula as rooms. Each 256-byte portrait
is far smaller than a room, so one ROML page holds 32 portraits
(`8192 / 256`), and 8 banks cover the full 256-ID range. `tools/pack_easyflash.py`
reads each populated portrait from `assets/portraits/NN`, flattened to
`PNN` alongside the other runtime assets; missing IDs are filled with `$FF`
like missing rooms.

### Generic resource directories

Rooms, object types, and portraits all use a fixed `bank = first_bank +
id / per_bank` formula because each is fixed-size and fully populated.
Variable-size or sparse content - the room/cutscene/conversation scripts
`modules/script.c` interprets (see `ROOM_CODE_API.md`'s "Room scripts"),
and similar - does not fit that formula, so it uses a directory instead,
the same idea as the room-code directory above but for plain data rather
than executable overlays.

Each of the three declaration kinds (script/conversation/room - see
`tools/compile_script.py`'s docstring) gets its own independent 256-entry
directory, so IDs don't need to be partitioned across kinds: a conversation
and a standalone script can both use ID 5 without colliding, and rooms can
use the full `0x00`-`0xFF` range. `PLATFORM_RESOURCE_KIND_SCRIPT` (0),
`_CONVERSATION` (1), `_ROOM` (2), `_ENVIRONMENT` (3), and `_ASSET` (4) in
`src/platform.h` select which. Kind 4 carries the static assets that used to
be linked into the program image - the two charsets and the tile
definitions+properties blob - fetched at boot straight to their reserved
addresses.

Banks 57-63 (the last 7 of the 64 available EasyFlash banks) are reserved
for this pool; no banks remain free after it. `RESOURCE_DIRECTORY_BANK`
(57) holds one 256-entry, 8-byte-per-entry directory per kind (2048 bytes
each, `$FF` for an unpopulated ID) back to back in
`PLATFORM_RESOURCE_KIND_*` order. Four of them exactly fill the 8 KiB ROML
half (script, conversation, room, environment), so the fifth kind - asset -
continues at the head of the **ROMH** half, and any kind added later does
the same. `resource_directory_lookup()` in `src/platform.c` and
`pack_resources()` in `tools/pack_easyflash.py` split on exactly that
boundary; the payload pool consequently starts after the ROMH directories,
not after the ROML ones. Each entry matches the room-code entry layout: bank,
mode, offset (little-endian), length (little-endian), and a 16-bit
sum-of-bytes checksum (little-endian). Mode selects which 8 KiB half of
that bank holds the payload -- `0` for ROML, `1` for ROMH -- so, unlike
room code, a resource can land in either half of a bank.
`tools/pack_easyflash.py` packs every kind's populated resources
first-fit (in `RESOURCE_KIND_ORDER`; ID order within a kind) into one
shared payload pool, starting right after the
three directories and filling each 8 KiB half completely before moving to
the next; a resource is capped at one 8 KiB half
(`PLATFORM_RESOURCE_MAX_BYTES`, `$2000`) and therefore never crosses an
EasyFlash bank switch.

Source files live at `assets/scripts/<ID>.script` (room),
`assets/scripts/conversations/<ID>.script` (conversation),
`assets/scripts/cutscenes/<ID>.script` (standalone script), or
`assets/resources/NN` (raw bytes, no header - shares the script kind's
directory, since it's likewise standalone content). Each is flattened to
`RRnn`/`RCnn`/`RSnn` respectively (kind prefix + hex ID) alongside the
other runtime assets, mirroring the portrait convention.

`platform_resource_fetch(kind, id, destination, capacity)` (see
`PLATFORM_API.md`) reads the matching directory's entry, then copies the
payload with the same `platform_easyflash_copy_roml()`/`copy_romh()`
primitives rooms, object types, and portraits already use, validates it
against the stored checksum, and returns the actual length or `0` if the
ID is unpopulated, oversized for `capacity`, or fails its checksum.
`platform_resource_fetch_range()` copies an arbitrary byte range instead
(no checksum - see `PLATFORM_API.md`), for a resource bigger than a
caller's resident buffer can hold in one piece; `modules/script.c` uses it
to keep a sliding window into a script resource up to the full 8 KiB half.

Placing a resource that must exceed one 8 KiB half is a known future
extension (a copy routine that increments `EASYFLASH_BANK` mid-copy when
crossing into a contiguous next bank) and is not implemented; today every
resource is bounded to one half so a naive byte-for-byte copy under one
bank selection is always correct.

A write to `$DE00` changes ROML and ROMH together. Code running from either
window must not switch away the bank containing its next instruction. Both the
bank-switch routine and its copy loop must therefore execute from stable RAM.

### Modules executed in place

Distinct from the loaded overlays above, a module can be linked inside ROML or
ROMH and **executed straight out of its bank**, never copied into RAM.
`modules/typeinfo.c`, `modules/inventory.c`, and `modules/room_helpers.c` use
this model.

The rules such a module follows:

- linked at its run address in `$8000-$9FFF` (ROML) or `$A000-$BFFF` (ROMH),
  equal to the selected window base plus its offset. Fixed coordinates live in
  `cfg/easyflash_layout.json`; the build generates the C and ca65 constants,
  and `tools/validate_easyflash_layout.py` verifies the linked entry and final
  packed bytes. The packer also guards against overlaps.
- a `jmp` entry vector at the module's fixed first address, so call sites do
  not have to track where the function itself lands after a relink
- **no BSS and no writable data of its own** - it is ROM at run time; any
  state it needs lives resident, outside the banking window
- **no self-modifying code** - writes beneath cartridge ROM reach underlying
  RAM, but subsequent instruction fetches still read the unchanged ROM. The
  old Inventory drawer's patched `STA` operand had to become a RAM-held
  indirect pointer for this reason
- reached through a resident C-callable stub that forwards via the `FAR_CALL`
  macro (`src/banked_api.s`), so callers need not know the routine moved

The far address includes two mapping fields, not one overloaded "mode":

| Service half | `$01` CPU map | `$DE02` control | `$A000-$BFFF` reads |
|---|---:|---:|---|
| ROML | `$37` | `$06` | BASIC ROM |
| ROMH | `$37` | `$07` | EasyFlash ROMH |

ROML is not asserted with `$01=$36`; that map exposes underlying RAM at
`$8000` and would make a ROML entry execute unrelated RAM. Therefore 8 KiB
execution prevents accidental ROMH selection but does not grant direct access
to upper RAM.

Unlike an overlay it needs no header, magic, or run-time checksum, because
nothing copies it. Build-time validators check its read-only segments, imported
addresses, linked entry point, packed bytes, and overlap bounds. A banked
routine may itself bank-switch only through a nesting-safe far/copy primitive:
Inventory enters bank 47 for type-info while running from bank 48, and
type-info fetches from bank 46; each nested switch restores the caller's bank
before execution resumes. A resident helper that directly changes `$01` is
not equivalent: if it restores `$35` before returning to a ROML caller, it
removes that caller's code. RH therefore moves its under-I/O hot-type access
outside the far call rather than treating a low-address callee as sufficient.

What a banked routine may touch is constrained by the mapping, not by the
cartridge - see `MEMORY_MAP.md`'s "Banked code and what it may touch".

### Object-type table placement

Only the fields object drawing and collision actually touch stay resident:
a 35-byte hot record (dimensions, hotspot, chars, colors, light) for all 256
IDs. Fetching a type from EasyFlash for every object cell would still be too
expensive and would complicate IRQ safety, so the hot table is copied into
RAM once at `platform_object_types_load()` and read directly after that.

Name and the actor-flag byte (15 bytes/record) are not resident at all --
they stay on EasyFlash bank 47 and are fetched with
`platform_object_type_info_get()` only where Take/Use/Look text or an
actor-flag check actually needs them (see `PLATFORM_API.md`). That table was
originally the full 64-byte record occupying the physical top 16 KiB
(`$C000-$FFFF`); the hot table alone needs only `$C000-$E301`, making
CPU-port state part of the platform ABI just for that smaller range:

- `$E000-$FFFF` is hidden by KERNAL ROM in the normal `$01` mapping;
- `$D000-$DFFF` is hidden by I/O while VIC, SID, CIA, and EasyFlash registers
  remain accessible;
- exposing RAM under `$D000` hides those I/O registers for the duration of the
  access;
- mapping out KERNAL means KERNAL keyboard and disk entry points must be mapped
  back in explicitly before calls.

Before any bank switching, set bits 0-2 of the data-direction register `$00` to
outputs. Change only bits 0-2 of `$01`, preserving its upper bits. With the
usual upper bits `$30`, the relevant mappings are `$37` for BASIC/KERNAL/I/O,
`$36` for RAM at `$A000` with KERNAL/I/O, `$35` for RAM at `$A000` and
`$E000` with I/O, and `$34` for RAM throughout `$A000-$FFFF` with I/O hidden.
The full baseline table is in `GUIDE_cc65_C64.md`.

The 35-byte hot record size does not divide 4 KiB evenly (`4096 / 35 = 117`
remainder 1), so each zone is sized to hold as many whole records as fit,
leaving a 1-byte gap before the next zone rather than splitting a record
across the I/O or KERNAL boundary:

| Type IDs | Address range | Access policy |
|---:|---|---|
| `0-105` | `$C180-$CFFD` | always-visible RAM |
| `106-222` | `$D000-$DFFE` | select all-RAM mapping briefly (1 free byte at `$DFFF`) |
| `223-255` | `$E000-$E482` | KERNAL out, I/O still visible |

Zone A no longer starts at `$C000`: the cc65 software stack (`$C000-$C0FF`)
and `GameState` (`$C100-$C173`) were moved into this region because it is the
only RAM above `$8000` that stays visible while a cartridge bank is mapped -
see `MEMORY_MAP.md`'s "Banked code". Zone A shrank to fit, and zone C grew to
take up the difference. `$E483-$FFF9` is free RAM.

A renderer can access types 0-105 directly, and types 223-255 while the
gameplay mapping is `$35`. For types 106-222 it must disable interrupts,
select the `$34`-equivalent low bits, copy the one 35-byte record to an
always-visible scratch record, restore `$35`, then render from scratch. It
cannot draw directly while `$D000` RAM is selected because screen colors and
VIC registers are hidden at the same time.

Loading the table needs the same staging rule. EasyFlash `$DE00/$DE02` vanish
when I/O is hidden, so data destined for `$D000-$DFFE` must first be copied from
ROML to visible scratch RAM, then EasyFlash must be disabled before selecting
all-RAM mode and copying the staged block to `$D000`.

The cold name/flags table is never staged into RAM at all: it is read
straight from EasyFlash into a small scratch buffer whenever
`platform_object_type_info_get()` is called.

It lives alone in **bank 46's ROMH half**, which nothing else uses, and is read
with a ROMH copy. It previously trailed the zone-C hot records in bank 47's
ROML half - but that half also hosts room-helpers, script and look-helpers
modules at fixed offsets, and those are packed afterwards, so they silently
overwrote it. Zone C + the cold table + those three overlays need roughly
9.5 KiB in an 8 KiB half, so the overlap was unavoidable rather than a tuning
mistake: every type from the room-helpers offset upward read overlay code
instead of its name and flags. Giving the cold table its own half fixed it and
freed bank 47's ROML layout to grow zone C.

The final six bytes of type 255's reserved area are runtime-owned RAM vectors
at `$FFFA-$FFFF`; type-table loaders restore them after writing the table.

### IRQ and KERNAL independence

Gameplay enters the raster handler directly through the RAM vector at
`$FFFE/$FFFF`. It saves A/X/Y, acknowledges the VIC source, and returns with
`RTI`. A second `$0314` entry uses the KERNAL restore path while disk code has
temporarily selected `$37`. IRQ code, stack, frame counter, and charset data
remain visible in both mappings.

Short keyboard calls can use `SEI`, map KERNAL in, call the routine, restore the
gameplay mapping, and `CLI`. Disk loading can be long enough that it needs a
separate loading-state design: blank or simplify the display and suspend the
custom raster IRQ, or provide both a direct RAM-vector entry and a KERNAL
`$0314` entry with the correct, different register-save/exit conventions.

EasyFlash wrappers use `SEI` only for the short map/register transition. The
raster IRQ's complete per-frame code and state are below `$8000`, never touch
EasyFlash registers, and may run while ROML/ROMH are selected. The complete
room transaction still disables the VIC raster source because it replaces the
IRQ-executed environment module as part of the transaction. It blanks the
screen and pins `$D018` before loading the room record or code. After the new
room is fully drawn, the transition resynchronizes `$D018` and the next compare,
then restores the previous VIC interrupt-enable state. Suspend/resume is
depth-counted.

The VIC display is deliberately in bank 3: tile/text charsets at
`$E800/$F000`, screen at `$F800`, and sprite data at `$FC00`. A tested bank-2
layout was rejected because the VIC saw EasyFlash ROMH in place of charset RAM
while Inventory executed from ROMH, producing corrupted text despite correct
underlying RAM. In bank 3 the VIC sees RAM beneath KERNAL independently of the
CPU. IRQ animation keeps a low-RAM shadow of the water glyph because CPU reads
from `$E800-$FFFF` still see KERNAL during disk calls.

The IRQ itself also treats late entry on lines 1-225 as a missed top event and
lines 227-311 as a bottom event, rather than waiting almost a complete frame.

The executable image also contains an independently linked resident helper and
bottom-text module after the main payload in bank 2. The RAM-resident bootstrap
copies it to `$3A00-$3BEF` before disabling EasyFlash; the pager's fixed entry
remains `$3A73`. Disk builds load the same `build/text.prg` as a second file,
so neither format requires zero padding from the end of resident code to
`$3A00`.

VICE can persist EasyFlash modifications back into the attached CRT on exit.
Do not leave an emulator attached to `build/game.crt` while rebuilding it: a
previous instance can overwrite the new image when it shuts down. Automated
tests should copy the CRT to a disposable path under `/tmp` and attach that
copy.

For larger projects, generate each physical bank in deterministic order:
bank 0 ROML, bank 0 ROMH, bank 1 ROML, bank 1 ROMH, and so on. Then combine the
program and asset banks before `cartconv` creates the CRT.

## Native drawing

Full room rendering uses an assembly blitter instead of 880 individual C cell
writes. It streams room tile IDs, indexes eight-byte definitions at `$3000`,
and write the four characters and colors directly to `$F800` and `$D800` while
maintaining pointers to two adjacent screen rows.

Those destinations are self-modifying operands. Both the left and right
character/color operands must be reset at the start of every full draw; the
previous call leaves them advanced beyond row 21. Resetting only the left
operands makes every other character of the next room overwrite row 22 and
also writes beyond the offscreen base-color buffer.

Object policy remains in C: slot order, player-last ordering, actor
ownership, room limits, and transitions. A native single-object renderer
handle the repetitive work: multiply the type ID by 64, unpack dimensions and
hotspot nibbles, clip signed coordinates, skip transparent character zero, and
write at most 16 character/color pairs.

The public C functions remain stable and call internal assembly fast
paths. cc65 assembly implementations must follow its calling convention: C
symbols have leading underscores, a single pointer normally arrives in A/X,
and routines with stacked arguments must perform the required callee cleanup.
Keep shared structure offsets and fixed addresses in an assembly include, with
C size assertions, so the C and assembly layouts cannot silently diverge.

Full map/object draws hide the sprite Look cursor before invoking the native
blitters. The cursor does not modify screen or Color RAM, so dirty-cell movement
does not require overlay bookkeeping. Movement should be profiled separately:
dirty-cell recomposition, not the full map blitter, is the relevant path for
actors.

## Flash saves are a separate feature (and are not planned)

EasyFlash flash ROM is not writable like RAM. An initialized C variable placed
in a cartridge segment is immutable game data, not a live save slot.

This project has decided against ever building a flash-save driver: saves are
disk-only on every build, cartridge included (see `SAVE_GAME.md`). Beyond the
real implementation cost below, some emulators require an explicit extra step
to persist cartridge RAM/flash writes back to the `.crt` file, which is the
opposite of what a save system needs. The list below stays as reference for
why this is genuinely hard, not as a to-do list.

A production flash-save implementation needs at least:

- a flash driver that executes entirely from RAM;
- explicit bank and cartridge-mode selection;
- command addresses appropriate to the ROML or ROMH flash chip window;
- sector erase handling before bits need to change from 0 back to 1;
- program/erase completion polling with timeout and error handling;
- protection against interruption or reset during a save;
- a save format that tolerates interrupted writes and flash wear;
- reserved sectors that cannot erase the bootstrap or game assets.

The common AMD-style unlock offsets are chip-relative. A store to CPU address
`$2AAA`, as shown in some simplified examples, writes C64 RAM and does not
address either EasyFlash cartridge window. Command addresses must be derived
from the selected flash chip and the EasyFlash mapping, preferably by using a
tested EasyFlash driver or EasyAPI rather than an ad hoc C routine.

The polling expression also needs care. Re-reading a volatile byte twice and
XORing bit 6 is not a complete driver: production code needs the device's
documented completion/error rules and a timeout.

No flash-save code is included in this repository yet.

## Corrections to common example layouts

| Common claim | Correct interpretation |
|---|---|
| Each bank is one linear 16 KiB ROM | Each bank selects separate 8 KiB ROML and ROMH regions together. |
| Cartridge vectors start at `$BFF6` | Ultimax CPU vectors use the final six ROMH bytes, physically `$BFFA-$BFFF`. |
| The `CBM` signature belongs near `$BFFA` | KERNAL autostart requires five-byte `CBM80` at `$8004-$8008`. |
| Code can switch its own ROM bank freely | Switching removes the executing bytes unless equivalent code exists in the new bank; use RAM code. |
| `#pragma code-name` places a writable save array | Code, read-only data, initialized data, and BSS are different linker concerns; flash is not writable RAM. |
| A partial binary always works with `cartconv -t easy` | VICE 3.9 requires `-p` for this non-padded image. |
| Three flash stores are enough for saving | Real saves require RAM-resident programming, erase management, polling, failure handling, and wear-aware data layout. |

### `$DE02` value table

The low three bits are `M`, `X`, and `G`. With software control enabled
(`M = 1`), use these complete register values:

| Value | Mode |
|---|---|
| `$04` | Cartridge ROM off |
| `$05` | Ultimax |
| `$06` | 8 KiB cartridge |
| `$07` | 16 KiB cartridge |

Reversing `$04` and `$07` causes an immediate boot failure: writing `$04`
while executing in ROML removes the next instruction from the CPU address
space. This was the cause of the first black-screen cartridge build.

## Current limitations

- Runtime room banks are fixed at 3-45; type pages are banks 46-47 (46 ROML
  holds zones A+B, 46 ROMH the cold name/flags table, 47 ROML zone C plus the
  in-place RH/Type Info services and the script/look-helpers overlays);
  portraits are banks 49-56; the generic resource directory reserves
  banks 57-63, the last banks the hardware supports -- no banks remain
  free after it.
- The complete PRG payload must fit in `$3D00` bytes of bank 0, bank 1, and
  the available portion of bank 2.
- PRG load and entry addresses are hard-coded in the bootstrap.
- EasyFlash remains off except during bounded runtime asset copies.
- There is no flash-save implementation, and none is planned; saves are
  disk-only (see `SAVE_GAME.md`).
- CRT structure and bounded VICE cold boots are part of verification.
