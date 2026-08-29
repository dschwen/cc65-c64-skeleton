# Save-game architecture

The save system separates immutable assets from mutable game state. Room files
remain the original `00` through `FF` assets in EasyFlash ROM. A running game
stores only object slots that differ from those base assets.

**Current status:** F1 Save, F3 Load, the slot-list browser, and disk I/O all
work and are verified in VICE against real `.d64` images. The former save
failure was a target-character-set bug: `cl65 -t c64` encoded assembly
`'W'` as high PETSCII `$D7`, so CBM DOS did not recognize `,W` as write mode.
`modules/disk_io.s` now emits DOS command/mode letters as explicit low
PETSCII bytes. After closing a save file, it reads the complete record back
and validates its length and checksum. See `SAVE_DISK_WRITE_BUG.md` for the
investigation.

## Implemented runtime layer

`src/world.c` installs the room store/restore hooks used by
`platform_room_enter()`:

- `$9D00-$9FFF` holds the pristine 768-byte object list for the current room;
- `$BC00-$BFE7` holds up to 200 five-byte `GameWorldDelta` records;
- each record contains room ID, exact object slot, and the three-byte object;
- a type-0 object is a deletion record, so taken items stay removed;
- returning to a room loads its immutable asset, captures that object list as
  the new baseline, and applies matching records by slot;
- the assembly slot preflight rejects a full effective destination before the
  restore hook replaces the leaving-room baseline;
- the active player slot is excluded because the player's type and hotspot are
  stored in `GameState` and transferred separately.

The store hook uses two passes. It counts the records needed before changing
the journal, so `PLATFORM_ERR_FULL` leaves both the previous journal and room
transition intact. Re-storing a room replaces that room's records and retains
all other room records.

`game_take_object(slot)` is the first save-aware mutation API. It rejects empty
slots, the player, and actors; adds the type to inventory; removes the room
object; and immediately captures the room delta. Any failure rolls back the
object and inventory.

Direct calls to `platform_room_object_add()`, `platform_room_object_remove()`,
or `platform_object_move()` do not capture immediately. Room code that uses
those low-level APIs must call `game_world_capture_current()` and roll back on
failure, or use a higher-level save-aware API as those are added.

## Resident API

```c
#include "world.h"

void game_world_init(void);
void game_world_reset(void);
uint8_t game_world_capture_current(void);

extern GameWorldDelta game_world_deltas[GAME_WORLD_DELTA_CAPACITY];
extern uint16_t game_world_delta_count;
```

`game_world_init()` is called once after `game_state_init()`. Reset discards all
room mutations and establishes the currently loaded room as pristine.
`game_world_capture_current()` returns `PLATFORM_OK`, `PLATFORM_ERR_FULL`, or
`PLATFORM_ERR_FORMAT`.

## Version 1 serialized record

The disk/flash backend should encode fields explicitly. Do not write the C
structures directly: compiler padding and later in-memory layout changes must
not silently change the file format. All multi-byte values are little-endian.

Header, 16 bytes:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `C64S` |
| 4 | 1 | format version, initially 1 |
| 5 | 1 | flags, initially 0 |
| 6 | 4 | reserved, zero |
| 10 | 2 | payload byte count |
| 12 | 2 | 16-bit payload checksum |
| 14 | 2 | reserved, zero |

Payload prefix, 130 bytes:

| Bytes | Meaning |
|---:|---|
| 16 | slot name: up to 16 printable characters (space, `0`-`9`, `A`-`Z`), zero-padded; unused trailing bytes are `0` |
| 4 | turn |
| 2 | day |
| 1 each | hour, minute |
| 1 each | current room, player type, player x, player y |
| 1 each | health, maximum health, mana, maximum mana |
| 64 | 32 inventory `(type, quantity)` pairs |
| 32 | global game flags |
| 2 | room-delta count |

The prefix is followed by `delta_count` five-byte records in journal order.
The maximum v1 record is 1,146 bytes including its header. Pending transitions
and `game_entry_reason` are transient and are not serialized. A save request
must finish or reject a pending room transition, then call
`game_world_capture_current()` before encoding.

## Disk backend

8 numbered slots (`0`-`7`) are offered; F1 opens the Save menu, F3 opens the
Load menu (both resident keys, wired in `src/main.c`, dispatching into an
EasyFlash overlay -- see "Save/load overlay" below). Storage is device 8.
Each slot has one sequential file named `S0` through `S7`.

`SINDEX` is a 146-byte, one-disk-block directory cache. Opening Save or Load
normally reads only this file, rather than probing every slot:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `C64I` |
| 4 | 1 | format version, 1 |
| 5 | 1 | entry size, 17 |
| 6 | 1 | slot count, 8 |
| 7 | 1 | reserved, zero |
| 8 | 2 | checksum of bytes 10-145 |
| 10 | 136 | eight entries: present byte plus 16-byte zero-padded name |

If `SINDEX` is absent or corrupt, the browser probes `S0` through `S7` once,
reads only each record header and name, rebuilds `SINDEX`, and then uses the
normal cached path. The recovery scan is intentionally exceptional because
eight missing-file timeouts are slow on a 1541.

Saving scratches and replaces `Sn`, closes it, reads the complete file back,
and validates its declared size and payload checksum. Only then does it
update and replace `SINDEX`. This deliberately uses one file per slot rather
than A/B generations: it halves directory clutter and I/O, but an interrupted
overwrite can lose that slot. A failed or partial index write is detected on
the next browser open and triggers the recovery scan.

KERNAL `CLOSE` only finishes sending the close request; a 1541 may still be
writing its final block and directory entry. The assembly writer therefore
opens and reads DOS status channel 15 after every data-file close. Record
readback and the final `Saved.` result occur only after DOS reports completion.

`make run-cartridge` creates `build/saves.d64` if needed and attaches it as
device 8. Launching `game.crt` by another method also requires a writable disk
image in drive 8; the EasyFlash image itself is not writable save storage.

The slot browser draws its screen once. Cursor movement changes only the old
and new caret cells. The inventory overlay uses the same incremental-caret
approach; it redraws the list only after `use` because story code may have
changed inventory contents.

Loading reads and validates the complete `Sn` record into `$A000-$A4E8`.
The browse overlay then returns before the room transition starts. This order
is mandatory: `platform_room_enter()` uses `$A000` for room staging and
`$A4E9` for room-code staging, so calling it while the save/load overlay was
still executing at `$A4E9` caused a reset/blue-screen crash. Resident code in
`src/saveload_runtime.c` now copies the decoded game state and journal to
their permanent locations, disables the leaving-room capture hook, enters
the saved room, restores the hook, sets `GAME_ENTRY_LOAD`, and invokes the
room's `enter_room()` and `enter_tile()` hooks.

## Save/load overlay

The implementation uses two independently linked overlays because their
combined UI and disk code does not fit the shared 4,119-byte window:

- `modules/saveload.c` (`SL`) provides the slot browser and full Load read;
- `modules/saveload_save.c` (`SV`) provides name entry, encoding, Save,
  readback verification, and index update.

They temporarily own `$A4E9-$B4FF`, the same rebuildable render/lighting RAM
used by the inventory overlay, and never run together. `SL` is stored in
EasyFlash bank 48 ROML. `SV` is stored in bank 47 ROMH. Each has a 16-byte
header (ABI 1) validated by the generic resident overlay loader also used by
`game_inventory_show()` (`platform_overlay_load()`,
`platform_overlay_validate_native()` in `src/platform.c`/`src/inventory_api.s`,
parameterized by bank, ROML/ROMH half, and expected magic bytes -- resident
code budget is too tight to duplicate that validator per overlay).

`src/main.c` maps `PLATFORM_KEY_SAVE` (F1) and `PLATFORM_KEY_LOAD` (F3) to
resident wrappers `game_save_show()`/`game_load_show()` (in
`src/saveload_runtime.c`, mirroring `game_inventory_show()`): load and
validate the overlay, run it in the requested mode, then restore the split
charset and redraw exactly like the inventory overlay's wrapper.

Inside the overlays, KERNAL disk calls (`SETLFS`/`SETNAM`/`OPEN`/`CHKIN`/
`CHKOUT`/`CHRIN`/`CHROUT`/`CLOSE`/`READST`) bracket each open file with
`platform_memory_kernal()`/`platform_memory_game()` (already resident,
previously unused) rather than a global `SEI`: KERNAL disk I/O needs
interrupts enabled for its own timing, and the raster IRQ already has a
`$0314`-vector entry point for exactly this "KERNAL mapped in" period (see
`EASYFLASH_CARTRIDGE.md`'s IRQ/KERNAL independence section). All save
storage uses device 8. Assembly reloads cc65's `ptr1` after KERNAL calls;
low zero page belongs to the KERNAL during those calls and cannot safely hold
a live C pointer.

## No EasyFlash-flash save backend

Saves are disk-only, on EasyFlash builds too: no native flash-write (EAPI)
save driver is planned. Building one is nontrivial (whole-erase-sector A/B
records, an IRQ-independent program/erase path in RAM) for a benefit that
does not hold up: EasyFlash cartridge storage is otherwise read-only for this
game, and some emulators require an extra explicit step to persist cartridge
RAM/flash writes back to the `.crt`, which is exactly the failure mode a save
system must not have. A cartridge build still writes saves through ordinary
disk KERNAL I/O, same as a disk build, using a disk device attached at
runtime purely for save storage.

## Capacity policy

The global journal and v1 file format both support 200 changed object slots.
The maximum record is 1,146 bytes and fits in the 1,257-byte `$A000-$A4E8`
room-staging area. Type 0 slots cost a record just like additions or movement.
When the journal is full, save-aware mutations and room transitions fail
without discarding prior state. Future formats can compact known one-shot
items into game flags while retaining v1 load compatibility.
