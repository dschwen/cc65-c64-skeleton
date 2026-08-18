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

Run it in VICE:

```bash
make run-cartridge
```

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
9. It writes a small disable-and-jump trampoline into screen RAM at `$0400`.
10. The trampoline disables EasyFlash and jumps to cc65 startup at `$080D`.
11. The game clears screen RAM during its normal initialization.

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

VICE 3.9 `cartconv` rejects a 32 KiB EasyFlash input by default because a
fully padded raw EasyFlash image would be 1 MiB. The build uses `-p` to accept
the non-padded 47-bank prefix; `cartconv` omits erased CHIP packets:

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

The implemented runtime presents one logical room-loading operation with two
storage implementations:

```c
typedef enum PlatformStorage {
    PLATFORM_STORAGE_DISK,
    PLATFORM_STORAGE_EASYFLASH
} PlatformStorage;

void platform_storage_init(PlatformStorage storage, uint8_t device);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
```

The disk backend retains the current two-hex-digit filename and `cbm_read()`
behavior. The EasyFlash backend obtains the same 1,257-byte record from an
asset bank. Game and rendering code must not care which backend supplied it.
The boot loader can leave a signature byte in reserved RAM so the common PRG
can select the EasyFlash backend when it was launched from a CRT.

Only the current room is resident for drawing. `platform_room_enter()` stages
and validates the destination, invokes the optional restore hook, checks the
arrival tile and target capacity, then invokes the optional store hook for the
leaving room. It commits only after those operations succeed, inserts the
actor in the first permitted empty slot, updates `platform_player_slot` and
`platform_player`, and redraws. `platform_room_object_transfer()` remains a
useful primitive for tools or code that deliberately keeps two room buffers.

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

The 8 KiB choice is deliberate. Selecting 16 KiB mode (`$07`) would make ROMH
hide `$A000-$BFFF`. Runtime ROML reads use CPU mapping `$36`: KERNAL and I/O
remain available, BASIC stays hidden, and the resident C stack at `$B900`
remains RAM. Gameplay mapping `$35` is restored when the cartridge is disabled.
Code and room staging remain
available throughout the copy. The copy path:

1. disable IRQs and remember the previous interrupt state;
2. write the asset bank to `$DE00`;
3. select 8 KiB mode with `$DE02 = $06`;
4. copy the requested bytes from `$8000-$9FFF` to unshadowed RAM;
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
BSS, both of which ROMH temporarily hides. See `ROOM_CODE_API.md`.

A write to `$DE00` changes ROML and ROMH together. Code running from either
window must not switch away the bank containing its next instruction. Both the
bank-switch routine and its copy loop must therefore execute from stable RAM.

### Object-type table placement

The 256 fixed 64-byte records occupy 16 KiB. Keeping them resident makes object
drawing and collision predictable; fetching a type from EasyFlash for every
object cell would be too expensive and would complicate IRQ safety.

The table occupies the physical top 16 KiB (`$C000-$FFFF`), making CPU-port
state part of the platform ABI:

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

The 64-byte type record size makes a `$C000` table particularly manageable:

| Type IDs | Address range | Access policy |
|---:|---|---|
| `0-63` | `$C000-$CFFF` | always-visible RAM |
| `64-127` | `$D000-$DFFF` | select all-RAM mapping briefly |
| `128-255` | `$E000-$FFFF` | KERNAL out, I/O still visible |

No record crosses a 4 KiB boundary. A renderer can access types 0-63 directly
and types 128-255 while the gameplay mapping is `$35`. For types 64-127 it
must disable interrupts, select the `$34`-equivalent low bits, copy the one
64-byte record to an always-visible scratch record, restore `$35`, then render
from scratch. It cannot draw directly while `$D000` RAM is selected because
screen colors and VIC registers are hidden at the same time.

Loading the table needs the same staging rule. EasyFlash `$DE00/$DE02` vanish
when I/O is hidden, so data destined for `$D000-$DFFF` must first be copied from
ROML to visible scratch RAM, then EasyFlash must be disabled before selecting
all-RAM mode and copying the staged block to `$D000`. Disk reads should also
stage that 4 KiB rather than point KERNAL I/O directly at `$D000`.

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

Short EasyFlash copies should still run under `SEI`. This avoids an IRQ seeing
ROML/ROMH unexpectedly or trying to use EasyFlash I/O while the copy routine is
changing its mode. Copy time must remain bounded so raster deadlines are not
missed; a 1,257-byte room copy may need to be scheduled during a blanked screen
or loading transition rather than during active display.

For larger projects, generate each physical bank in deterministic order:
bank 0 ROML, bank 0 ROMH, bank 1 ROML, bank 1 ROMH, and so on. Then combine the
program and asset banks before `cartconv` creates the CRT.

## Native drawing

Full room rendering uses an assembly blitter instead of 880 individual C cell
writes. It streams room tile IDs, indexes eight-byte definitions at `$3000`,
and write the four characters and colors directly to `$0400` and `$D800` while
maintaining pointers to two adjacent screen rows.

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

Full map/object draws hide the sprite dialog before invoking the native
blitters. Dirty-cell movement remains overlay-aware because it updates the
saved-color backing array. Movement should be profiled separately: dirty-cell
recomposition, not the full map blitter, is the relevant path for actors.

## Flash saves are a separate feature

EasyFlash flash ROM is not writable like RAM. An initialized C variable placed
in a cartridge segment is immutable game data, not a live save slot.

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

- Runtime room banks are fixed at 3-45; type pages are banks 46-47.
- The complete PRG payload must fit in `$3D00` bytes of bank 0, bank 1, and
  the available portion of bank 2.
- PRG load and entry addresses are hard-coded in the bootstrap.
- EasyFlash remains off except during bounded runtime asset copies.
- There is no flash-save implementation.
- CRT structure and bounded VICE cold boots are part of verification.
