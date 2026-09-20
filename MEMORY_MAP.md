# Runtime memory map

This is the authoritative allocation for the Oscar64 EasyFlash build.
Layout is defined by pragmas in `src/memmap.h` and the banked source files;
`build/game.map` and `build/game.lbl` contain the current symbol addresses.
The former cc65 `cfg/*.cfg` layouts no longer apply.

## Resident RAM and VIC assets

| Address/range | Owner |
|---|---|
| `$0000-$0001` | 6510 direction and CPU mapping ports |
| `$0002-$007F` | Oscar64 runtime registers/parameters; allocation varies |
| `$0080-$00FF` | KERNAL/editor zero-page workspace |
| `$0100-$01FF` | 6502 hardware stack |
| `$0200-$03FF` | KERNAL/editor workspace, keyboard buffer and RAM vectors |
| `$0400-$07FF` | KERNAL initialization screen and cartridge bootstrap scratch |
| `$0800-$08FF` | Oscar64 startup |
| `$0900-$7DFF` | main: resident code, initialized data, BSS and stacks |
| `$7E00-$7FFF` | relocated room environment module; excluded from main |
| `$8000-$BFFF` | selected EasyFlash ROM bank, never resident workspace |
| `$C000-$CFFE` | hot object records 106-222 (4,095 bytes) |
| `$CFFF` | unallocated |
| `$D000-$DFFF` | VIC/SID/CIA, Color RAM and EasyFlash registers |
| `$E000-$E7FF` | underlying RAM unused; CPU sees KERNAL |
| `$E800-$EFFF` | tile charset |
| `$F000-$F7FF` | text charset |
| `$F800-$FBE7` | 40 x 25 screen matrix |
| `$FBF8-$FBFF` | sprite pointers |
| `$FC00-$FDFF` | eight 64-byte sprite slots |
| `$FFFA-$FFFB`, `$FFFE-$FFFF` | RAM NMI/IRQ vectors for brief KERNAL-out access |

Main includes object records 0-105, room buffers, tile data/properties,
world deltas and all service state. These are compiler-allocated, not the old
cc65 fixed addresses. The software stack reservation is 2 KiB; both it and
Oscar64's static frames must end before `$7E00`. Banked code may call
resident functions and use resident state, but cannot read another bank's
code/data without switching banks.

Environment banks use a dedicated entry section ordered before constants
and code. The first eight bytes copied to `$7E00` are always the init/tick/
enable/disable function pointers, even for modules with music lookup tables.

## Startup contract

Oscar64's `-tf=crt` startup expands **only main** from bank 0 and clears
main BSS. Merely declaring an initialized array in another RAM region does
not put that region into the boot image.

`platform_init()` runs with interrupts masked and:
1. Calls KERNAL IOINIT and RESTOR; sets the KERNAL screen page to `$04`
   before CINT initializes editor/keyboard state.
2. Disables CIA interrupts and initializes the default I/O channels.
   RAMTAS must not be called after Oscar64 startup: it clears live zero page.
3. Configures VIC bank 3 and 40-column hires text, initially blanked.
4. Copies the tile/text charsets from bank 15 to `$E800/$F000`, and hot
   object zone B from bank 16 to `$C000`; then loads tiles and the room.
5. Installs the raster/NMI vectors before enabling interrupts.

Zone A is initialized as main data. Zone B is explicitly loaded at boot
(the compiler can reduce its copy to a fill when every source byte is zero).
Zone C (types 223-255) remains in bank 16 with a resident one-record cache.
Tiles and properties are reloadable because save/load borrows their storage.

## CPU and cartridge mapping

Gameplay and KERNAL calls use `$01` low bits `111` (normally `$37`).
This exposes KERNAL and I/O as well as the 16 KiB EasyFlash bank window.
Clearing HIRAM hides cartridge ROM too; a banked routine cannot execute
through that change. Preserve the cassette-port bits when changing mapping.

`$DE00` is write-only. `platform_cart_bank` is the authoritative software
shadow; `far_call_bank()` saves/restores it around nested banked calls.
All banked entry functions are `__noinline`.

CPU writes to `$E000-$FFFF` reach underlying RAM while KERNAL is visible,
but reads return ROM bytes. The resident `platform_copy_vic_ram()` gate
briefly masks IRQs, selects `$35`, copies between addresses outside
`$8000-$BFFF`, then restores both the port and processor status.
Water initialization and text scrolling use this gate. Dirty redraw writes
screen cells directly rather than comparing against ROM.

## Raster split and input

CIA2 port A bits 0-1 are outputs driven low, selecting VIC bank 3.
The VIC independently sees RAM beneath KERNAL. `$D018=$EA` selects the
tile charset; `$EC` selects text. Raster zero restores tiles, and the
line-226 IRQ switches at line 227 for the bottom status rows. Full-screen
menus select text throughout. Resynchronization after input schedules the
next event; it must not wait a whole frame with IRQs masked.

The custom raster handler replaces the KERNAL timer IRQ. Foreground code
calls SCNKEY/GETIN once per frame, using initialized keyboard state and
the restored GETIN vector at `$032A`. Display literals use mixed-case
PETSCII, matching prepared assets; disk command literals retain low PETSCII.
Water/rain and environment ticks are serviced from the foreground frame wait.

## Verification

Build with `make OSCAR64=/path/to/oscar64`. Run
`xvfb-run -a python3 tools/test_oscar_boot.py` (VICE and python3-xlib required).
The test checks cold-boot RAM against assets, KERNAL/CIA state, real XTest
keyboard input, Look/cancel, inventory round-trip, movement, rain, the
VIC's charset selection on visible map/text scanlines, and transitions through
the tavern/kitchen/cellar/corridor environment modules. It saves screenshots,
a monitor transcript and a failure snapshot. No keyboard-buffer injection
or emulator RAM corrections are used in the regression. Room scenarios queue
the game's normal pending-transition request, then verify environment entries
and advancing SID music state.
