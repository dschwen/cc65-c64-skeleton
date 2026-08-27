# Save-game architecture

The save system separates immutable assets from mutable game state. Room files
remain the original `00` through `FF` assets in EasyFlash ROM. A running game
stores only object slots that differ from those base assets.

**Current status:** F3 Load, the slot-list browser, and disk reads all work
and are verified in VICE (confirmed against a real d64: peeking 16 candidate
files on an empty disk correctly reports every slot empty, with no hang or
corruption). F1 Save's UI, encoding, and KERNAL call sequence are all in
place, but the disk **write** does not currently work: `platform_disk_write_block()`
in `modules/disk_io.s` reports success (clean `READST`, byte count matches)
after `CHKOUT`/`CHROUT`/`CLOSE`, but no file appears afterward -- confirmed
both by inspecting the resulting `.d64` from outside VICE and by an
immediate in-session readback of the same filename failing, and the drive's
own error channel reporting 62 (FILE NOT FOUND) after a write the
data-channel status had already claimed succeeded. The identical KERNAL call
sequence (scratch, then `SETLFS`/`SETNAM"...,W"`/`OPEN`/`CHKOUT`/`CHROUT`/
`CLOSE`) works correctly in a plain BASIC-booted VICE session against the
same device and image, so this is specific to running from this game's
cartridge/overlay context; root cause not yet found despite testing simpler
filename forms, bypassing the scratch step, suspending the custom raster IRQ,
and re-enabling the CIA1 jiffy-clock interrupt around the write. See the
`KNOWN ISSUE` comment at the top of `modules/disk_io.s`.

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
| 6 | 4 | monotonically increasing generation |
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

## Disk backend plan

8 numbered slots (`0`-`7`) are offered; F1 opens the Save menu, F3 opens the
Load menu (both resident keys, wired in `src/main.c`, dispatching into an
EasyFlash overlay -- see "Save/load overlay" below). Each slot uses two
files on device 8, for example `S0A` and `S0B`. Saving writes the
older/invalid side with generation + 1, closes it, then verifies its header
and checksum. Loading validates both files and chooses the newest valid
generation. A torn write therefore leaves the other file usable. The
implementation should stream rather than require another 1.2 KiB buffer.

Both menus open on a slot-list screen built by reading, for each slot's
newest valid generation, only the 16-byte file header plus the leading 22
payload-prefix bytes (name, then turn/day) -- enough to show a name and a
short summary without decoding the whole record. An unreadable/absent slot
shows as empty. Save additionally prompts for a name (defaulting to the
slot's existing name, if any) before writing the complete record. Loading a
slot runs the full transactional load below; saving over an occupied slot
overwrites both its name and content.

Loading is transactional:

1. decode and validate the complete candidate into temporary game state and
   journal storage;
2. validate all object types, slots, coordinates, counts, and health/mana
   bounds;
3. temporarily disable the leaving-room store hook;
4. load the saved base room and apply the candidate journal;
5. insert the saved player through the normal room transition path;
6. load and activate that room's code overlay;
7. commit `GameState` and the journal only after every step succeeds;
8. set `game_entry_reason = GAME_ENTRY_LOAD`, redraw, and call `enter_room()`
   followed by `enter_tile()`.

The temporary decode area can reuse `$A4E9-$B4D8`, whose render buffers are
rebuildable. Code performing the load cannot run from the room overlay while
that overlay is replaced; it needs a resident trampoline or a dedicated save
overlay with a resident completion step.

## Save/load overlay

`modules/saveload.c` is an independently linked overlay -- the disk I/O,
slot-list UI, and name entry all live there, not resident, for the same
reason as the inventory overlay (see `STORY_CODE_API.md`): it temporarily
owns `$A4E9-$B4D8`, the same rebuildable render/lighting work RAM the
inventory overlay uses, and the two never run at once. EasyFlash stores its
loadable bytes in bank 48 ROML (the inventory overlay already owns that
bank's ROMH half, so this adds no new bank), with its own 16-byte header
(`SL` magic, ABI 1) validated by the same generic, resident overlay loader
`game_inventory_show()` uses (`platform_overlay_load()`,
`platform_overlay_validate_native()` in `src/platform.c`/`src/inventory_api.s`,
parameterized by bank, ROML/ROMH half, and expected magic bytes -- resident
code budget is too tight to duplicate that validator per overlay).

`src/main.c` maps `PLATFORM_KEY_SAVE` (F1) and `PLATFORM_KEY_LOAD` (F3) to
resident wrappers `game_save_show()`/`game_load_show()` (in
`src/saveload_runtime.c`, mirroring `game_inventory_show()`): load and
validate the overlay, run it in the requested mode, then restore the split
charset and redraw exactly like the inventory overlay's wrapper.

Inside the overlay, KERNAL disk calls (`SETLFS`/`SETNAM`/`OPEN`/`CHKIN`/
`CHKOUT`/`CHRIN`/`CHROUT`/`CLOSE`/`READST`) bracket each open file with
`platform_memory_kernal()`/`platform_memory_game()` (already resident,
previously unused) rather than a global `SEI`: KERNAL disk I/O needs
interrupts enabled for its own timing, and the raster IRQ already has a
`$0314`-vector entry point for exactly this "KERNAL mapped in" period (see
`EASYFLASH_CARTRIDGE.md`'s IRQ/KERNAL independence section). All save
storage uses device 8.

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

The initial global journal limit is 200 changed object slots. It is deliberately
smaller than the per-room object address space and bounded so memory use is
predictable. Type 0 slots cost a record just like additions or movement. When
the journal is full, save-aware mutations and room transitions fail without
discarding prior state. Future formats can raise the limit or compact known
one-shot items into game flags while retaining v1 load compatibility.
