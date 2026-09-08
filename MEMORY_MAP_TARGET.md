# Banked-memory rework: status and plan

Goal: move the game off "copy everything into RAM" and onto EasyFlash banks -
code executed in place, static assets fetched to fixed destinations - so
resident RAM holds only what genuinely must be resident.

`MEMORY_MAP.md` describes the layout as it *is*. This file records what has
been done, what is blocked and why, and the order the remaining work has to
happen in. Update it as steps land.

## Done

| Step | Effect |
|---|---|
| `KIND_ASSET` resources | Charsets and tile data are fetched at boot instead of linked in. Byte-neutral so far (see below), but they are now swappable and no longer image-constrained. |
| `FAR_CALL` + trampoline | Banked routines callable by far address (bank + 16-bit offset), reentrant, with a cc65 `A`/`X` argument and return convention. |
| Software stack -> `$C000` | 256 bytes against a measured peak of 37. Prerequisite for banked C: cc65 code touches its stack constantly, and `$BA00` was inside the banking window. |
| `GameState` -> `$C100` | Banked code can read game state directly. |
| `_raster_irq_resync` -> `HIGHCODE`, its flags -> `LOWBSS` | The bank machinery calls resync on every unwind; it and its flags were inside the window, so banked code could not use the bank machinery at all. |
| Cold object-type table -> bank 46 ROMH | Fixed a real pre-existing bug: the overlays were overwriting it, so every type above the room-helpers offset returned overlay code instead of its name and flags. |
| First in-place banked module | `platform_object_type_info_get()` runs from bank 47, and itself fetches from bank 46 while doing so. |

## The rule everything is constrained by

While a far call runs, `$8000-$BFFF` is cartridge ROM. A banked routine may
read only `$0000-$7FFF` and `$C000-$CFFF`, and may only call resident code
living outside that window. Writes still reach the RAM underneath; only reads
are affected.

`$C000-$CFFF` is the only RAM above `$8000` that no memory map ever covers,
which is why the stack and `GameState` went there and why it is now nearly
full (stack 256 + `GameState` 116 + object-type zone A 3,710 of 4,096).

## Cost model

Both bank-switch paths wait for the raster to wrap before disabling
interrupts: free on raster lines 0-255, up to ~56 lines otherwise. One coarse
call around a large piece of work pays ~1%; the same work split into many
small banked calls pays proportionally more. **Bank whole operations, not
inner-loop helpers.** This does not rule out banking the renderer - a single
call around a ~300 ms draw is noise - it rules out per-object far calls.

## Blocked, and why

The VIC bank move (screen and sprites to `$8000-$85FF`, charsets to
`$A000-$AFFF`, freeing `$2000-$2FFF` and `$3A00-$3BBF` below `$8000`) does not
currently fit. Evacuating those two ranges means rehoming:

| Block | Size |
|---|---:|
| `ROOMRAM` + `STATEEXT` (`$8000-$85FF`) | 1,536 |
| `ROOMSTAGE` (`$A000-$A4E8`) | 1,257 |
| overlay window (`$A4E9-$B4FF`, must stay contiguous) | 4,119 |
| **total** | **6,912** |

Available is ~1,792 left in the window after the VIC takes its share plus
~4,851 below `$8000` (including what the move itself frees) = **6,643**. Short
by roughly 270 bytes. Moving `WORLDDELTA` out would let the overlay window sit
at `$B000-$BFFF` - 4,096 bytes against the 4,107 the save-detail overlay
actually uses. Eleven bytes short. Not a margin worth building on.

The answer is not to find 270 bytes: it is to **delete the overlay window**,
which is 4,119 bytes of RAM whose only job is holding a copy of code that
already exists in a bank.

## Order of remaining work

1. **Bank the overlays** (`RH`, `SC`, `LH`, `IV`, `SL`, `SV`) using the
   in-place pattern. The blocker found earlier - they call `UPPERCODE` at
   `$8B48`, which a mapped bank hides - dissolves if their callees are banked
   too and reached by far call, since the trampoline is reentrant. Start with
   room-helpers (579 bytes, smallest).
2. That frees the 4,119-byte overlay window, which unblocks the VIC move.
3. **VIC bank move**, freeing `$2000-$2FFF` and `$3A00-$3BBF` below `$8000`.
4. **Move the renderer's hot data** there. Measured need: `platform_room`
   1,373 + `platform_base_colors` 880 + `platform_brightness` 220 +
   `platform_light_visibility` 220 + `platform_view_tiles` 220 + scratch and
   `rendered_*`/`native_*` ~60 = **~4,350**, against ~4,850 available. Note
   `base_colors` and `brightness` currently live in `WORKBSS`, i.e. inside the
   overlay window, so they are already rebuilt rather than persistent.
5. **Bank the renderer** as a single coarse call.

Only after (1) does the previously circular dependency break: the VIC move
needed the overlay window gone, which needed overlays banked, which needed
space the VIC move would have provided.

## Dead ends, recorded so they are not retried

**Overlay window to `$E400` (under-KERNAL RAM).** Links cleanly, boot works,
room code and object types still stage correctly through it - but overlay
validation then rejects every overlay at the checksum step, so none run.
Confirmed a regression against the previous commit. The payload lands
byte-identical in RAM and the file's checksum is self-consistent; only the
checksum loop's reads disagree. Suspected cause: the ROMH copy path maps the
KERNAL over the destination (`CPU_MAP_CART_16K`, HIRAM=1) while copying into
it, so writes pass through but reads there return ROM. Do not retry without
first proving CPU reads at the new base under every map the load path passes
through. Kept as git stash "overlay window -> E400 WIP".

**Marking the vacated charset areas `file = ""`.** A PRG is a load address
followed by contiguous bytes, so removing bytes from the middle makes
everything after load at the wrong address; `tools/validate_prg_layout.py`
catches it. The areas must keep reserving their place until the charsets
physically leave low RAM, which is what the VIC move does. This is why the
asset split is byte-neutral until step 3.
