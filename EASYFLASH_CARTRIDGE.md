# EasyFlash runtime contracts

Oscar64 emits the cartridge directly with `-tf=crt`. This document covers
game-specific decisions, not another compiler/packer tutorial. For compiler
pragmas see [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md); for RAM ownership see
[MEMORY_MAP.md](MEMORY_MAP.md).

## Build and startup

`make` and `make cartridge` compile/link all sources in one invocation after
preparing assets and generating room/script C. There are no per-bank linker
passes, external CRT conversion, or runtime resource directories/checksums.
The resulting `build/game.crt` is the game image; a PRG is not a supported
runtime alternative.

Compiler startup expands main from bank 0 into `$0900-$7DFF` and initializes
normal BSS. `platform_init()` establishes KERNAL/CIA keyboard state and VIC
bank 3, copies charsets from `BANK_ASSET_TILES` and object zone B from `BANK_OBJECT_TYPE_C`, and
installs interrupts. Arrays assigned to other RAM regions are not implicitly
included in the startup copy. Never call RAMTAS after compiler startup:
it clears live compiler zero page.

The CRT contains read-only game content. `make run` attaches a separate
writable save disk on unit 8; no EAPI flash-write backend is implemented.

## Bank allocation

Declarations in `src/banks.h` and region pragmas are authoritative; exact
object offsets come from `build/game.map`, not hand-maintained packed offsets.

| Bank | Content |
|---|---|
| 0 | Compiler startup and compressed resident main |
| 1 | Cold object metadata (names and flags) |
| 2 | Room helpers |
| 3 | Look helpers |
| 4 | Script interpreter |
| 5 | Inventory and story inventory actions |
| 6 | Save/load browser |
| 7 | Save encoding and write workflow |
| 8 | KERNAL disk I/O |
| 9 | Tile/text charset sources, tile definitions and properties |
| 10 | Object zone B boot source and zone C records |
| 11 | World-delta store/restore code |
| 12 | Title screen: picture, credits and the Moonfen Waltz player |
| 13 | Rain environment (rooms 00, 01) |
| 14 | Tavern environment (02) |
| 15 | Muffled tavern environment (03, 05) |
| 16 | Cellar environment (04) |
| 17 | Compiled portrait sprite images and bank-local fetch dispatcher |
| 18 | Compiled room-script bytecode and bank-local fetch dispatcher |
| 19 | Compiled conversation bytecode and bank-local fetch dispatcher |
| 24-63 | Rooms: room N's immutable record and native handlers in bank 24+N (IDs 0-39) |

Banks have a 16 KiB CPU window, `$8000-$BFFF`. Oscar64 rejects two regions
assigned to the same bank ("Overlapping data section"), so a collision is a
build error rather than a silent overlap; still check `src/banks.h` before
extending a contiguous range. The generated resource banks and room code
name their bank through the `src/banks.h` symbols, so the numbers live only
there.

## Banked code and data

`far_call_bank(bank, function)` executes a no-argument entry through resident
code, saving/restoring the software bank shadow across nested calls.
Adapters/shared resident request fields handle arguments/results where needed.
Entries must be `__noinline`; writable globals and compiler stacks remain
outside the ROM window. Calls within the selected bank are ordinary calls;
cross-bank calls need a resident gate. A native function pointer alone cannot
select its bank.

The steady CPU map has LORAM/HIRAM/CHAREN set (usually `$37`). Do not clear
HIRAM from a routine executing in ROM: it hides the very instructions being
executed. The resident `platform_copy_vic_ram()` gate temporarily selects
`$35` for high-RAM reads, copies only between addresses outside the cartridge
window, and restores mapping and processor status before returning.

Interrupt handlers never switch banks. Long banked calls may leave the raster
IRQ running, but block foreground gameplay until they return.
Bank complete operations, not pixel-loop helpers, and audit the full call/data
closure (including literals, compiler helpers, indirect targets and pointers).

## VIC assets remain beneath ROM

The tile charset is at `$E800`, text at `$F000`, screen at `$F800`, sprite
pointers at `$FBF8`, and sprite slots at `$FC00`. These are physical VIC-bank-3
RAM beneath KERNAL; they do not consume the low main region. Do not move them
into ordinary low RAM to work around a CPU-read bug.

CPU writes reach the underlying RAM, but reads with KERNAL visible return ROM.
Read-modify-copy operations such as text scrolling and initial water-shadow
capture must use the resident gate. The VIC sees the underlying RAM
independently. Keep the project's proven bank-3 placement rather than moving
assets into the cartridge's `$8000-$BFFF` CPU window.

## Room and environment entry contracts

Native room handlers execute in their assigned ROM bank. The room record is
first in that bank, copied to resident storage at entry. There is no copied
room-code overlay or on-disk ABI header. See [ROOM_CODE_API.md](ROOM_CODE_API.md).

Environments are different: each is linked for `$7E00-$7FFF` and copied there
from its source bank. The first eight bytes are four two-byte native pointers
in order: init, tick, enable, disable. A dedicated entry section must precede
data and code; a music constant table at offset zero would be called as a
function address and can jam the CPU. Mutable environment globals are resident.

Room entry suspends raster work while changing the environment and drawing;
resume before calling anything that waits for a frame or paged text.
Environment ticks run from the foreground frame wait, not directly in the IRQ.
Portrait pause/resume uses the same environment enable/disable interface.

## Resources and verification

Room scripts are compiled from `assets/scripts/NN.script`, combined by
`tools/generate_script_bank.py`, and embedded into `BANK_SCRIPT_ROOM_FIRST`.
`assets/scripts/conversations/NN.script` (decimal IDs) is compiled and
embedded the same way, into `BANK_SCRIPT_CONVERSATION_FIRST` - a separate
bank because conversations have an independent 0-255 ID space from rooms.
The script opcode numbering is shared through `src/script_format.h`, which
both the interpreter and `tools/compile_script.py` read. `PLATFORM_RESOURCE_KIND_ROOM` and `_CONVERSATION` are implemented by
the generic fetch API (src/resources.c dispatches on kind). Charsets, tiles,
object records and environments have their own direct paths; reserved
resource-kind constants do not imply implemented loaders.

Run `make test` for cold-boot RAM, real keyboard input, visible raster
phases, rain, room environment entry pointers, advancing SID state, an F1
save / F3 load round trip on a disposable D64, and a typed-keyword
conversation (assets/scripts/conversations/05.script). Artifacts include
monitor logs/screenshots and a failure snapshot. Tests queue normal pending
room transitions; they do not yet verify every walking route, missing/
corrupt-index disk recovery, audible music quality or NTSC timing.
