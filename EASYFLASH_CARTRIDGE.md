# EasyFlash Cartridge Generation

This document records the design decisions and practical lessons behind this
repository's EasyFlash build. It describes the current two-bank cartridge,
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
| `build/game-ef.bin` | Raw 32 KiB contents of EasyFlash banks 0 and 1 |
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
payload. For this project's two-bank image, `cartconv -f` reports:

```text
CHIP FLASH #000 $8000 $2000
CHIP FLASH #000 $a000 $2000
CHIP FLASH #001 $8000 $2000
CHIP FLASH #001 $a000 $2000
```

These are ROML and ROMH for banks 0 and 1. `cartconv` omits erased banks, so
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
- code executing inside a banked window cannot switch away its own bank;
- ROML/ROMH hide RAM or BASIC ROM beneath their CPU windows;
- VIC-visible assets still need a deliberate RAM/VIC-bank strategy.

The current cartridge instead treats the working PRG as its payload:

1. The normal `make` build produces `build/game.prg`.
2. `ef_boot.s` embeds the PRG excluding its two-byte load address, placing the
   first `$3000` bytes in bank 0 and the remainder in bank 1.
3. The bootstrap initializes CPU port registers `$01` and `$00`.
4. It selects EasyFlash bank 0 and 16 KiB mode.
5. It calls KERNAL `IOINIT`, `RAMTAS`, `RESTOR`, and `CINT`.
6. It copies the bank 0 chunk into RAM beginning at `$0801`.
7. It copies a position-independent second-stage loader to `$C000` and jumps
   there before selecting bank 1 through `$DE00`.
8. The RAM stage copies the bank 1 remainder into contiguous destination RAM.
9. It writes a small disable-and-jump trampoline into screen RAM at `$0400`.
10. The trampoline disables EasyFlash and jumps to cc65 startup at `$080D`.
11. The game clears screen RAM during its normal initialization.

The trampoline is necessary because an instruction following `sta $DE02`
could no longer be fetched from ROML after the cartridge is disabled.
The `$C000` stage is necessary for the same reason when `$DE00` selects bank
1: bank 0 ROML, including the first-stage loader, disappears immediately.

This design keeps the PRG and cartridge builds behaviorally aligned and makes
the cartridge a fast, self-contained loader. The cartridge is not banked in
during normal gameplay.

## Current bank layout

`cfg/easyflash.cfg` creates two filled 16 KiB raw banks:

| Range | Segment | Current use |
|---|---|---|
| `$8000-$8008` | `CART_HEADER` | Vectors and `CBM80` signature |
| `$8009-$807D` | `BOOT` | RAM initialization and copy loader |
| `$807E-$80FF` | fill | `$FF` padding |
| bank 0 `$8100-$B0FF` | `PAYLOAD0` | first `$3000` PRG payload bytes |
| bank 0 `$B100-$BFF9` | fill | `$FF` padding |
| `$BFFA-$BFFF` | `VECTORS` | Ultimax NMI, RESET, and IRQ vectors |
| bank 1 `$8000+` | `PAYLOAD1` | remaining PRG payload bytes |
| remainder of bank 1 | fill | `$FF` padding |

The first payload starts at `$8100` to leave room for the bootstrap. The
assembler asserts a full `$3000`-byte first chunk, a nonempty second chunk,
and that the remainder fits in bank 1.

The current bootstrap has two intentional hard-coded couplings to the PRG
linker layout:

- PRG load address: `$0801`
- cc65 startup entry: `$080D`

If `cfg/myc64.cfg` changes either value, update `PRG_START` and `CC65_START`
in `cart/ef_boot.s`. Also verify the payload still fits across banks 0 and 1.

## Linker and `cartconv` pipeline

The cartridge is built in three stages:

```text
C/assembly/assets -> game.prg
game.prg + ef_boot.s + easyflash.cfg -> game-ef.bin
game-ef.bin + cartconv -> game.crt
```

The raw linker output is ordered exactly as one EasyFlash bank expects:

```text
8 KiB bank 0 ROML, bank 0 ROMH, bank 1 ROML, then bank 1 ROMH
```

VICE 3.9 `cartconv` rejects a 32 KiB EasyFlash input by default because a
fully padded raw EasyFlash image would be 1 MiB. The build uses `-p` to accept
the non-padded two-bank binary:

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

Verify both embedded chunks against the PRG excluding its load address:

```bash
cmp -i 256:2 -n 12288 build/game-ef.bin build/game.prg
payload1_size=$(( $(wc -c < build/game.prg) - 2 - 0x3000 ))
cmp -i 16384:12290 -n "$payload1_size" build/game-ef.bin build/game.prg
```

The second byte count is computed from the current PRG size and changes with
the game. The linked `PAYLOAD0`/`PAYLOAD1` sizes are also recorded in
`build/game-ef.map`.

Structural checks do not replace a cold-boot test in VICE and, ideally, on
real EasyFlash hardware. In the development environment used to add this
target, both installed VICE executables exited with status 139 during host
video initialization, so runtime testing could not be completed there.

VICE also warns when an EasyFlash CRT does not contain the optional EasyAPI
signature in bank 0 ROMH. That warning is expected for this read-only game and
does not indicate a malformed boot image. Do not add a fake signature: include
a real, current EasyAPI only when flash writing is implemented.

The reserved EasyAPI location is bank 0 ROMH offset `$1800`, written in the
EasyFlash guide as `00:1:1800`. In this linker's physical ROMH address space it
is `$B800-$BBFF`. A valid image begins there with lowercase `eapi` bytes and
contains the actual flash driver. This project's range remains erased `$FF`.

## Expanding beyond bank 1

The loader uses `$DE00` to copy bank 1, then disables the cartridge. To use
additional banks during gameplay:

1. Decide on the raw ordering: bank 0 ROML, bank 0 ROMH, bank 1 ROML, bank 1
   ROMH, and so on.
2. Generate a padded 1 MiB image or a correctly ordered non-padded prefix.
3. Keep bank-switching code in stable RAM or in a ROM region that will remain
   selected throughout the switch.
4. Select 16 KiB mode and write the bank number to `$DE00`.
5. Copy assets from `$8000-$9FFF` or `$A000-$BFFF` into their runtime RAM
   locations, or process them while the selected bank is visible.
6. Disable the cartridge again if the game expects RAM/BASIC beneath those
   windows.

A write to `$DE00` changes ROML and ROMH together. Code running from either
window must not switch to a bank that lacks the next instruction. A small RAM
routine is generally the simplest safe bank-copy primitive.

Interrupt behavior must also be deliberate. If an IRQ can run while a bank is
temporarily selected, its code and data must not depend on memory hidden by
ROML or ROMH. Disable interrupts around short bank-copy operations or design
the IRQ memory layout to be independent of cartridge state.

For larger projects, generating each 16 KiB bank separately and combining
them in a deterministic bank-ordering step is often clearer than forcing many
overlapping `$8000/$A000` linker regions into one ld65 configuration.

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
| A 16/32 KiB binary always works with `cartconv -t easy` | VICE 3.9 requires `-p` for this non-padded 32 KiB input. |
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

- Only EasyFlash banks 0 and 1 are populated.
- The complete PRG payload must fit in `$3000` bytes of bank 0 plus one full
  16 KiB bank 1.
- PRG load and entry addresses are hard-coded in the bootstrap.
- The cartridge is disabled during gameplay; there is no runtime asset-bank
  API beyond the bootstrap copy.
- There is no flash-save implementation.
- CRT structure has been validated, but this environment could not execute a
  VICE cold-boot test because of its host video crash.
