# C64 Game Platform API

The public runtime API is declared in `src/platform.h` and implemented in
`src/platform.c`. It provides fixed-size room storage, tile and object
rendering, minimal object movement redraws, room-transition detection, bottom
status text, lighting/visibility, and a sprite tile cursor without dynamic
allocation.

See `MEMORY_MAP.md` for the complete CPU-layer map, current linker occupancy,
RAM beneath BASIC/I/O/KERNAL, and portrait-sprite allocation.

## Coordinate systems

The platform uses three related coordinate systems:

| Unit | Range | Purpose |
|---|---|---|
| tile | x `0-19`, y `0-10` | room maps and 2x2-character tiles |
| half-tile / character | x `0-39`, y `0-21` | objects, movement, hotspots |
| pixel | 320x200 display | sprite hardware only |

A half-tile is exactly one 8x8 screen character. Object positions store the
half-tile coordinate of the object's hotspot. The graphic origin is:

```text
left = object.x - type.hotspot_x
top  = object.y - type.hotspot_y
```

This makes the hotspot the authoritative cell for collision, trigger, and
room-transition logic even when a graphic extends in several directions.

## Fixed memory ownership

| Address/range | Owner |
|---|---|
| `$0400-$07E7` | screen matrix |
| `$07F8-$07FF` | eight sprite pointers |
| `$2000-$27FF` | tile charset |
| `$2800-$2FFF` | text charset |
| `$3000-$38FF` | tiles and tile properties |
| `$3900-$39FF` | compact read-only lookup tables |
| `$3A00-$3BFF` | eight 64-byte sprite bitmap slots; look cursor uses slot 0 |
| `$3C00-$7FFF` | platform code and read-only tables |
| `$8000-$84E8` | current room RAM (1,001-byte `PlatformRoom`; the linker region is still sized 1,257 bytes from before the room-text-pool removal, so 256 bytes here are currently unclaimed slack) |
| `$84E9-$855C` | fixed `GameState` |
| `$855D-$85FF` | compact native resident helpers |
| `$8600-$8B47` | resident world-state code |
| `$8B48-$98FF` | resident game/main and shared room-API code |
| `$9900-$9CFF` | active 1 KiB room-code overlay |
| `$9D00-$9FFF` | pristine current-room object baseline |
| `$A000-$A4E8` | destination-room staging (1,001-byte `PlatformRoom`; like `$8000-$84E8`, the linker region is still sized 1,257 bytes, so `script_resource_kind` - src/script_runtime.c - borrows one of the 256 otherwise-unclaimed slack bytes rather than costing BSSRAM, which has none free) |
| `$A4E9-$B4FF` | rebuildable work RAM; inventory/story/save overlay while active |
| `$B500-$B80C` | ordinary resident BSS |
| `$B80D-$B9FF` | independently loaded helpers and bottom-text pager |
| `$BA00-$BBFF` | free (the software stack moved to `$C000`) |
| `$BC00-$BFFF` | sparse room-object delta journal |
| `$C000-$C0FF` | cc65 software stack |
| `$C100-$C173` | persistent `GameState` |
| `$C180-$FFFF` | 256 resident object-type records |

The platform preallocates:

- one 1,001-byte `PlatformRoom` (in a 1,257-byte linker region - see the memory map above);
- one byte each for `platform_current_room` and `platform_player_slot`;
- one `PlatformObject* platform_player` pointing into the current room list;
- 256 object-type records, 16 KiB total;
- a 110-byte dirty-cell bitmap.

No API allocates heap memory.

## Room file format

Room files are named by the uppercase two-digit hexadecimal room ID: `00`
through `FF`. Each file is exactly 1,001 bytes.

| Offset | Size | Content |
|---:|---:|---|
| `0` | 1 | width, always 20 |
| `1` | 1 | height, always 11 |
| `2` | 1 | room ID |
| `3` | 1 | format version, currently 3 |
| `4` | 1 | valid-exit mask: north/east/west/south in bits 0-3 |
| `5` | 1 | north neighbor room ID |
| `6` | 1 | east neighbor room ID |
| `7` | 1 | west neighbor room ID |
| `8` | 1 | south neighbor room ID |
| `9` | 1 | north exit-description byte - currently unread, reserved |
| `10` | 1 | east exit-description byte - reserved |
| `11` | 1 | west exit-description byte - reserved |
| `12` | 1 | south exit-description byte - reserved |
| `13` | 220 | tile IDs, 20x11 row-major |
| `233` | 768 | 256 three-byte object slots |

An object slot is:

| Byte | Meaning |
|---:|---|
| 0 | object type ID; 0 means empty |
| 1 | hotspot x in half-tiles |
| 2 | hotspot y in half-tiles |

A room's text and simple logic (Look/Use/room-entry/tile-entry hooks) is not
part of this file at all: it's a compiled *room script*, a same-ID resource
(`assets/resources/<hex room id>`) authored as DSL source
(`assets/scripts/<hex room id>.script`, `tools/compile_script.py`) and run via
`game_room_script_entry()` (see "Room-code helper API" below and
`ROOM_CODE_API.md`). The four exit-description bytes above are reserved for a
future version of this same mechanism applied to exit descriptions - not
wired up yet, so `platform_look_exit()` always shows a generic message.

The exit mask is separate from the four neighbor bytes because every byte
value, including room `FF`, is a valid destination. Links may be one-way. The
editor imports legacy, format-1, and format-2 rooms and exports format 3 (any
embedded text those legacy formats carried is dropped on import - see the
asset editor README). Use `tools/migrate_rooms_v2.py` and then
`tools/migrate_rooms_v3.py` for batch migration.

## Object-type file format

`objects.cobj` is exactly 16,384 bytes: 256 records times 64 bytes. Type ID is
the record index. Type 0 is reserved for an empty object slot.

| Record offset | Size | Content |
|---:|---:|---|
| 0 | 1 | width in high nibble, height in low nibble |
| 1 | 1 | hotspot x in high nibble, hotspot y in low nibble |
| 2 | 14 | name, zero-padded |
| 16 | 16 | row-major character codes; 0 is transparent |
| 32 | 16 | row-major C64 color indices |
| 48 | 1 | flags; bit 0 means PC/NPC actor, bit 1 means not takeable |
| 49 | 1 | emitted light amount; 0 means no light |
| 50 | 14 | unused padding; dropped at build time, not carried anywhere |

The editor stores names as ASCII. The same build preparation step converts the
14-byte name fields to PETSCII before the game loads them.

Width and height are each limited to 1-15, and `width * height` must not
exceed 16. Only the first `width * height` character/color entries are used.

The 16 color bytes resolve an ambiguity in the original proposed record: an
object graphic needs both a full eight-bit character code and a color. They
occupy half of the originally proposed 32 future bytes.

This file format is authoring-only. `tools/pack_easyflash.py` splits every
64-byte record at build time into the resident `PlatformObjectType` (35
bytes: width/height, hotspot, chars, colors, light) and the cartridge-only
`PlatformObjectTypeInfo` (15 bytes: name, flags); see "Object types: hot/cold
split" below and `EASYFLASH_CARTRIDGE.md`. The 14 padding bytes are dropped
by that split, not preserved anywhere.

## Initialization and loading

```c
void platform_init(void);
void platform_room_state_hooks(PlatformRoomStoreHook store_hook,
                               PlatformRoomRestoreHook restore_hook);
void platform_room_clear(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id);
uint8_t platform_object_types_load(void);
const PlatformObjectType* platform_object_type_get(uint8_t type_id);
const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id);
uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y);
```

Both state hooks return a platform status. The store hook runs while the
leaving-room baseline is current; the restore hook runs on the staged
destination and may reject it before commit. Any non-`PLATFORM_OK` result
aborts the transition without removing the current player.

Call `platform_init()` once before other platform APIs. It selects VIC bank 0,
sets border/background black, selects the tile charset, installs the raster IRQ,
and initializes the platform. Room `00` and types 0-1 baked into the PRG are a
placeholder fallback for the (unexpected) case where the EasyFlash cartridge
boot marker is absent; the real room/type data always loads from EasyFlash.
These globals are established:

```c
platform_current_room = 0;
platform_player_slot = 0;
platform_player = &platform_room.objects[0];
```

Thus the player is the actual slot-0 object from room `00`, not a detached
copy. The current asset defines it as actor type 1, "The Hero".

`platform_room_load()` selects the fixed ROML bank and offset for `room_id`,
reads into staging RAM, validates the 1,001-byte record, and only then commits
it to the caller's room. Loading into the global `platform_room` also updates
`platform_current_room` and rebinds `platform_player` to `platform_player_slot`.
`platform_object_types_load()` reads the resident hot fields for all 256
types (see "Object types: hot/cold split" below). Both return:

| Result | Meaning |
|---|---|
| `PLATFORM_OK` | success |
| `PLATFORM_ERR_IO` | open/read failure or truncated input |
| `PLATFORM_ERR_FORMAT` | invalid room header |
| `PLATFORM_ERR_ARGUMENT` | invalid pointer/argument |

Example:

```c
uint8_t result;

platform_init();
result = platform_object_types_load();
if (result == PLATFORM_OK) {
    result = platform_room_load(&platform_room, 0x2a);
}
```

### Object types: hot/cold split

Only the fields the render/collision path actually touches stay resident:
`platform_object_type_get()` returns a 35-byte `PlatformObjectType`
(dimensions, hotspot, chars, colors, light) for all 256 IDs, backed by three
resident zones at a fixed 35-byte stride -- always-visible RAM for IDs
0-105, RAM beneath I/O for 106-222 (copied to scratch with IRQs disabled),
RAM beneath KERNAL for 223-255 (same). Zone A starts at `$C180` rather than
`$C000` because the software stack and `GameState` were moved into that region
(see "Banked code" below). The returned pointer is only valid
until the next call to either accessor below.

Name and the actor-flag byte are not resident at all. Call
`platform_object_type_info_get(type_id)` to fetch the 15-byte
`PlatformObjectTypeInfo` (name, flags) directly from its fixed EasyFlash
bank/offset -- a real cartridge bank-switch, heavier than
`platform_object_type_get()`. This accessor is itself the first routine that
*runs from a bank in place* rather than resident (see "Banked code" below), so
a call now costs a far call plus its own nested fetch. Only call it from discrete, human-input-paced
code (Take/Use/Look text, actor-flag checks before adding/counting objects),
never per frame or per rendered cell. `PLATFORM_OBJECT_FLAG_ACTOR` now tests
`info->flags`, not a field on the hot record.

This reclaimed 7,416 bytes of RAM beneath KERNAL (`$E302-$FFF9`) that the
previous 64-byte-per-record resident table fully occupied, without dropping
any of the 256 logical type IDs. See `EASYFLASH_CARTRIDGE.md` for the bank
layout `tools/pack_easyflash.py` produces.

### Dynamic-room storage contract

The platform loads rooms throughout gameplay rather than preloading all 256
rooms. Rooms are immutable base records copied from banked EasyFlash ROM into
the single resident `platform_room`. All game-asset reads (rooms, object
types, portraits, the inventory/story overlay, room code) are EasyFlash-only;
disk KERNAL I/O is reserved for save games (see `SAVE_GAME.md`), which is
specified but not yet implemented.

A room transition using one resident buffer follows this order:

1. clear rows 22-24, disable the raster source, and force tile charset bank 0;
2. retain the transitioning player/NPC record outside the room buffer;
3. load and validate the destination immutable room;
4. remove its baked startup-player record when applicable;
5. collision-check the arrival tile and prepare the destination room overlay;
6. capture modifications to the leaving room while its baseline is current;
7. preflight an actor slot, establish the destination baseline, and apply deltas;
8. insert the actor into the first valid empty slot;
9. update `platform_current_room`, `platform_player_slot`, and
   `platform_player` only after success;
10. activate the room overlay and draw tiles, objects, and the player;
11. resynchronize the split and reenable the raster source.

Every failure after step 1 follows the same resume path, so an I/O, format,
collision, room-code, or capacity error cannot leave the raster IRQ disabled.

`src/world.c` supplies that mutable policy with exact-slot deltas against the
immutable asset. The current `platform_room_object_transfer()` still expects
both rooms in RAM and is a low-level primitive; use save-aware game APIs for
single-buffer gameplay. See `SAVE_GAME.md` for invariants and capacity.

The EasyFlash backend, bank layout, RAM copy primitive, and KERNAL/IRQ
constraints are specified in `EASYFLASH_CARTRIDGE.md`.
Room-specific handlers, `GameState`, and the room-code ABI are specified in
`ROOM_CODE_API.md`.

## Map and room drawing

```c
void platform_map_draw_tile(uint8_t tile, uint8_t tile_x, uint8_t tile_y);
void platform_map_draw(const PlatformRoom* room);
void platform_object_draw(const PlatformObject* object);
void platform_room_draw(const PlatformRoom* room,
                        const PlatformObject* player);
```

`platform_room_draw()` enforces this z-order:

1. all 220 tiles;
2. populated room object slots in ascending slot order;
3. the player pointer last.

The player may point at an object inside `room->objects`. That exact slot is
skipped in step 2 and drawn in step 3, so player records can remain in the room
list while still receiving top visual priority. Pass `NULL` for no player.

Character code 0 in an object type is transparent: neither screen RAM nor
the offscreen base-color buffer is changed for that object cell. Objects are
clipped to the 40x22 map area.

The native renderer preserves these public functions. Assembly is an
implementation detail: the map blitter streams the 220 tile IDs and writes
screen RAM plus the offscreen base-color buffer; the object blitter draws one
clipped, transparent object at a time. A final native pass expands the 20x11
tile-brightness buffer over the 40x22 base colors in Color RAM. Room ordering and
transition policy remain in C. A full room draw also clears screen and Color
RAM rows 22-24 before rendering, preventing separator/status text from the
previous room or a full-screen text view from surviving the transition.

## Lighting

```c
extern uint8_t platform_base_colors[40 * 22];
extern uint8_t platform_brightness[20 * 11];
extern uint8_t platform_global_light;
extern uint8_t platform_view_tiles[20 * 11];
extern const uint8_t platform_light_colors[4 * 16];
extern const uint8_t platform_light_distance[16 * 16];

void platform_lighting_apply(void);
void platform_lighting_set_global(uint8_t level);
void platform_lighting_rebuild(const PlatformRoom* room,
                               const PlatformObject* player);
void platform_lightning(void);
```

Brightness is per 2x2-character tile. Object hotspots remain at half-tile
resolution but light sources snap to the tile containing the hotspot. Levels
are `PLATFORM_LIGHT_NONE` (0),
`PLATFORM_LIGHT_DIM` (1), `PLATFORM_LIGHT_TWILIGHT` (2), and
`PLATFORM_LIGHT_FULL` (3). Map and object rendering preserve original colors
in `platform_base_colors`; `platform_lighting_apply()` changes only the 880 map
entries in Color RAM. The two status rows are deliberately unaffected.

`platform_lighting_rebuild()` fills the buffer with the global ambient level,
then max-combines every room-object and player light source before applying the
result. It also rebuilds the internal viewer-quadrant cache for opaque tiles.
`platform_lighting_set_global()` clamps invalid input to full light and performs
that rebuild immediately. The sample game binds `-` and `+` to decreasing and
increasing ambient light. Direct brightness-buffer edits must be followed by
`platform_lighting_apply()`.

Object-type byte 49 remains a radius in half-tile/character-cell units and is
clamped to 16 at runtime. Tile distance is doubled before applying the existing
bands, preserving asset compatibility: remaining radius 4 or more is full,
2-3 is twilight, and 0-1 is dim. Thus radius 10 gives full light through three
tiles, twilight at four tiles, and dim at five tiles. Sources use max
composition, so overlapping lights never reduce an existing level.

Distance is ceiling Euclidean tile distance. `platform_light_distance` is the
first-quadrant lookup indexed by `(abs_y << 4) | abs_x`; its result is doubled
before comparison with the half-tile radius. Positions outside the radius are
excluded.

Tile-property bit 1 (`PLATFORM_TILE_BLOCKS_VIEW`) blocks both illumination and
player sight. A native one-parent ring propagation produces one 20x11 mask per
emitter and a persistent 360-degree player mask. Visible opaque tiles propagate
occlusion outward; every next-ring tile has exactly one writer, so states never
merge. The native lighting pass writes final lit or black colors per 2x2 tile
without exposing a fully lit intermediate map. Player visibility is recomputed
only when half-tile movement crosses a tile boundary.

Emitter masks also apply a viewer-relative wall-facing rule. An opaque tile is
not illuminated by a source when its X or Y coordinate lies strictly between
the source hotspot and player hotspot on that axis. The tile remains an
occluder, so illumination is still stopped behind it. This test runs per source:
a light on the player's side can illuminate the wall even when a different
light on the far side cannot. Player visibility is built first and remains a
separate final mask over the composed brightness buffer.

For each opaque tile, the platform caches four 2-bit maxima in one byte: one for
each viewer quadrant relative to that tile. Moving a non-emitting player across
a tile boundary rebuilds the persistent player visibility mask and selects new
wall brightness from this cache. It does not clear the 220-byte brightness
buffer, recast any emitter, or rerun open-cell distance falloff. Moving an
emitter, changing ambient light, loading a room, or explicitly rebuilding
lighting invalidates and recreates the cache.

`platform_base_colors`, `platform_brightness`, room staging, and the internal
wall cache live in `$A000-$BFFF` RAM beneath BASIC ROM. They are accessible in
the normal gameplay mapping (`$01` low bits `101`) but temporarily hidden
whenever `platform_memory_kernal()` maps BASIC/KERNAL ROM in for a KERNAL call
(disk save I/O, once implemented, is the only caller). Code bracketing such a
call owns that mapping interval; game code must not access these buffers
concurrently with it.

Emitter propagation is native assembly. C resolves the potentially banked
object-type record, clamps the radius, and prepares a clipped rectangle. The
assembly loop patches its brightness destination and distance-table row once
per tile row, derives each band from `radius - 2 * tile_distance`, and performs
a strict max write. There is no C call or multiplication in the per-tile path.

`platform_lightning()` has no manual test keybinding; it's triggered from a
room script via the `lightning` statement (see `tools/compile_script.py`'s
module docstring and `modules/script.c`'s `OP_LIGHTNING`). Its assembly routine
sets the VIC border and background to white, clears only the 40x22 map portion
of Color RAM to black, waits for two raster-frame counter changes, restores the
VIC colors to black, and tail-calls the native lighting pass to reconstruct
Color RAM from the base-color and brightness buffers. Screen RAM and the two
bottom text rows are not touched. Raster interrupts must be enabled so the frame
counter can advance.

The lookup table is indexed as `(brightness << 4) | (base_color & 15)`. Its
four rows and object-emitter propagation are documented in
[LIGHTING.md](LIGHTING.md). In the twilight row black remains black; none
of the other 15 input colors maps to itself.
Full map/object draws hide the look cursor before invoking the native blitters.
Dirty-cell movement does not need special cursor handling because a sprite does
not alter screen or Color RAM.

## Object movement and lists

```c
void platform_object_move(PlatformRoom* room, PlatformObject* object,
                          uint8_t new_x, uint8_t new_y,
                          const PlatformObject* player);

uint8_t platform_room_object_add(PlatformRoom* room, uint8_t type,
                                 uint8_t x, uint8_t y, uint8_t* out_slot);
uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player);
uint16_t platform_room_object_count(const PlatformRoom* room,
                                    uint8_t actor_only);
uint8_t platform_room_object_transfer(PlatformRoom* leaving,
                                      PlatformRoom* entering,
                                      uint8_t leaving_slot,
                                      uint8_t new_x, uint8_t new_y,
                                      uint8_t* entering_slot);
```

`platform_object_move()` minimizes screen writes as follows:

1. mark nontransparent cells at the old position in a 40x22 deduplication bitset and a compact `(x,y)` list;
2. update the hotspot position;
3. mark nontransparent cells at the new position;
4. for each marked cell, compose tile + ordered objects + player;
5. compare the result with current screen/color RAM;
6. write only bytes that differ.

This correctly restores exposed tiles and overlapping objects without
redrawing the room or blindly writing the entire bounding rectangle. The list
contains at most 32 cells because an object type has at most 16 cells at each
of its old and new positions. Redraw iterates only that list, not all 880 map
cells, and object composition stops at the rendered room's highest populated
slot rather than testing all 256 slots for every cell.

`platform_room_object_add()` uses the first type-0 slot. It rejects the add
when all 256 slots are occupied or when the room already has 200 non-actor
objects. Actor types are identified by `PLATFORM_OBJECT_FLAG_ACTOR` in the
object-type flags byte. `out_slot` may be `NULL`.

`platform_room_object_count()` accepts:

- `0`: all populated objects;
- `1`: actors only;
- `2`: non-actors only.

`platform_room_object_transfer()` first attempts to append to the entering
room. It clears the leaving slot only after the add succeeds, so a full target
room does not lose the object. The caller must have both rooms loaded and is
responsible for persisting their modified files/state.

## Game loop, keyboard, and player movement

```c
extern volatile uint8_t platform_frame_counter;

void platform_wait_frame(void);
uint8_t platform_input_poll(void);
uint8_t platform_player_step(int8_t delta_x, int8_t delta_y);
```

The bottom-of-map raster IRQ increments `platform_frame_counter` once per
video frame. `platform_wait_frame()` waits for that byte to change, providing
a 50 Hz PAL or 60 Hz NTSC game-loop cadence.

`platform_frame_counter` cannot advance while `raster_irq_suspend()` holds the
raster IRQ masked (every room transition does, for its whole duration - see
`platform_room_enter()`'s contract above). `platform_wait_frame()` detects
this and falls back to polling the VIC's raster position directly instead of
hanging - real hardware timing, independent of whether an interrupt is
allowed to fire. This matters for anything built on `platform_wait_frame()`:
`game_wait_fresh_key()`, the bottom pager's between-pages wait, and the
`wait_key` script opcode all now work correctly (they used to hang) when
triggered from a room's `enter_room()`/`enter_tile()`, which always runs
inside that suspended window during a transition - see
`MEMORY_MAP_TARGET.md`'s "Transition hang" section for the full story and a
remaining, lower-severity glitch (narration shown this way still renders
through the wrong charset, readable but wrong, until the transition
finishes).

Because the custom raster IRQ exits through the KERNAL IRQ restore path rather
than running the normal KERNAL handler, it does not scan the keyboard itself.
`platform_input_poll()` is an assembly wrapper that calls KERNAL `SCNKEY` and
then `GETIN`. Call it once after each `platform_wait_frame()`; calling it in an
unthrottled busy loop would advance KERNAL debounce/repeat state too quickly.
It returns zero when no event is available. Cursor-key values are exposed as:

| Key | Constant | Value |
|---|---|---:|
| Down | `PLATFORM_KEY_CURSOR_DOWN` | 17 |
| Right | `PLATFORM_KEY_CURSOR_RIGHT` | 29 |
| Up | `PLATFORM_KEY_CURSOR_UP` | 145 |
| Left | `PLATFORM_KEY_CURSOR_LEFT` | 157 |
| Look | `PLATFORM_KEY_LOOK` | 76 (`L`) |
| Select | `PLATFORM_KEY_ENTER` | 13 (Return) |
| Take | `PLATFORM_KEY_TAKE` | 84 (`T`) |
| Use | `PLATFORM_KEY_USE` | 85 (`U`) |
| Inventory | `PLATFORM_KEY_INVENTORY` | 73 (`I`) |

`platform_player_step()` proposes a signed half-tile delta. An in-room
destination must have `PLATFORM_TILE_SOLID_LAND` (`$04`) set. Crossing an edge
uses the corresponding enabled neighbor and enters at the opposite boundary
while preserving the orthogonal coordinate. Only the hotspot is tested; the
dimensions of the player graphic do not expand the collision footprint.

An edge crossing is queued through the same deferred `game_transition_request()`/
`game_process_pending_transition()` mechanism a room script's
`room_transition` uses (see `ROOM_CODE_API.md`), not applied inside
`platform_player_step()` itself - see "Screen transitions" below for why.
The caller sees `PLATFORM_OK` the instant the step off the edge is accepted;
the destination room's data, code, and environment module load, and its
`enter_room()`/`enter_tile()` run, one frame later. This is not observable as
input lag (it is the same one-frame latency scripted transitions already
have), but it does mean `game_state.current_room` does not change until the
following frame - do not assume it reflects an edge crossing immediately
after `game_player_step()` returns.

The demo becomes interactive immediately after drawing the first room. Cursor
events normally move the player by one half-tile. While look mode is active,
the same keys move the tile cursor and Return selects its tile.

Take and map Use share a cursor constrained to the player's surrounding 3x3
tile area. Take selects intersecting non-actor objects; Use dispatches the
selected coordinates to the active room's `use_at()` hook. Inventory uses a
full-screen text overlay: cursor keys select a carried stack, `U` calls the
global `story_use_inventory()` hook, and `I` returns to the map.

```c
for (;;) {
    platform_wait_frame();
    key = platform_input_poll();
    /* Dispatch cursor constants to platform_player_step(). */
}
```

## Screen transitions

```c
uint8_t platform_transition_check(const PlatformRoom* room,
                                  const PlatformObject* object,
                                  int8_t delta_x, int8_t delta_y,
                                  uint8_t* stepped_tile);
```

This tests a proposed half-tile movement without changing the object. It
returns one of:

- `PLATFORM_TRANSITION_NONE`;
- `PLATFORM_TRANSITION_TOP`;
- `PLATFORM_TRANSITION_LEFT`;
- `PLATFORM_TRANSITION_BOTTOM`;
- `PLATFORM_TRANSITION_RIGHT`;
- `PLATFORM_TRANSITION_TRIGGER`.

An in-room result writes the proposed hotspot's tile ID to `stepped_tile` when
that pointer is non-NULL. Trigger detection uses tile property bit 4.

Format 3 stores the four edge destinations and descriptions.
`platform_room_neighbor()` returns
`PLATFORM_ERR_NOT_FOUND` when a direction is disabled. Before committing an
edge transition, `platform_room_enter()` loads and validates the destination,
suppresses the baked player spawn when returning home, checks arrival land,
prepares its room code, stores the leaving room, and applies the destination
restore hook. The restore hook reserves an actor slot before changing its
baseline. Only then does the platform insert the actor and commit the staged
room. Any failure leaves the current room and player intact; a successful
leaving-room capture is harmless and idempotent if a later preflight fails.

The tile-cursor Look command keeps its frame inside the map; pushing outward
at an edge displays a generic exit message without loading the neighboring
room (`platform_look_exit()` - custom per-exit descriptions aren't wired up
yet; see the room file format section above).

Every caller of `platform_room_enter()` must bracket it - together with the
`game_enter_room()`/`game_enter_tile()` sync that follows it, in one unbroken
window - with `platform_screen_blank()`/`_unblank()` and
`raster_irq_suspend()`/`_resume()` (see `platform_room_enter()`'s own comment
in `src/platform.h`). There are exactly three such callers, all going through
`game_process_pending_transition()`'s bracket shape: `src/main.c`'s startup
sequence, `game_process_pending_transition()` itself (scripted transitions
*and*, since the fix described in `MEMORY_MAP_TARGET.md`'s "Interrupt/banking
safety audit", ordinary edge-of-room walking, both queued through
`game_transition_request()`), and `saveload_apply_pending()`. Do not add a
fourth call site that invokes `platform_room_enter()` directly outside that
bracket - queue a transition through `game_transition_request()` instead.

Trigger destinations remain game-defined. Game code resolves:

```text
(current room, edge direction)
or
(current room, trigger tile/position)
```

to a destination room and hotspot before calling `platform_room_enter()`.

## Bottom text API

```c
void game_text_write(uint8_t line, const char* text, uint8_t color);
uint8_t platform_text_screen_code(char ch);
void platform_text_screen_enter(void);
void platform_text_screen_leave(void);
void platform_text_clear_line(uint8_t line);
void platform_text_write_line(uint8_t line, uint8_t column,
                              const char* text, uint8_t color);
uint8_t platform_look_tile_check(const PlatformRoom* room,
                                 const PlatformObject* viewer,
                                 uint8_t tile_x, uint8_t tile_y,
                                 uint8_t color);
uint8_t platform_look_exit(const PlatformRoom* room,
                           const PlatformObject* viewer,
                           uint8_t edge_x, uint8_t edge_y,
                           uint8_t direction, uint8_t color);
uint8_t platform_look_tile(const PlatformRoom* room,
                           uint8_t tile_x, uint8_t tile_y, uint8_t color);
uint8_t platform_object_intersects_tile(const PlatformObject* object,
                                        uint8_t tile_x, uint8_t tile_y);
void platform_object_take_prompt(uint8_t type_id, uint8_t color);
void platform_object_taken_message(uint8_t type_id, uint8_t color);
```

Line 0 is screen row 23; line 1 is row 24. The raster IRQ has already selected
charset bank 1 for these rows.

Game and room code should normally use `game_text_write()` for a literal
string, or - for a room's own text/logic - `game_room_script_entry()`
(`ROOM_CODE_API.md`), which prints via the same word-wrapped pager
internally (the script interpreter's `TEXT` opcode). `game_text_write()`
clears the status area, wraps at word boundaries, and splits words longer
than 40 characters. When output needs a third line, the pager waits for a
fresh press and release, moves the lower line to the upper line, clears the
lower line, and continues. Explicit carriage returns and line feeds also
advance through the same pager. The implementation is an assembly module
whose helper block loads at `$B80D`; the pager entry remains `$B880`.

The lower-level `platform_text_write_line()` call remains available for
fixed-position UI and clips at column 40; it does not invoke wrapping or
paging.

`platform_text_screen_enter()` tells the assembly raster IRQ to keep charset
bank 1 selected at raster line zero, making all 25 rows text rows.
`platform_text_screen_leave()` restores the normal tile-map/text-line split.
These calls only select the charset; a full-screen UI owns clearing, drawing,
and restoring screen and Color RAM while the mode is active.

For C string literals, the text charset convention is:

- lowercase ASCII `a-z` maps to screen codes 1-26;
- uppercase ASCII `A-Z` maps to screen codes 65-90;
- other bytes pass through unchanged.

Example:

```c
platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
platform_text_write_line(PLATFORM_TEXT_LINE_TOP, 1,
                         "YOU FOUND A KEY", 1);
```

`platform_look_tile_check()` must run before room-specific descriptive code. It
first checks `platform_view_tiles`, so an occluded tile reports `I cannot see
that.` without invoking the room hook. It then takes the maximum brightness of
the tile's four character cells and compares Chebyshev distance from the
viewer against the provisional ranges: no light is never readable, dim light
reaches 2 tiles, twilight reaches 6, and full light reaches every tile allowed
by line of sight. A darkness failure reports `It is too dark to make anything
out. I need to get closer!`. The range table is deliberately isolated for the
planned perception formula.

`platform_look_tile()` groups repeated object types and writes their names
through the word-wrapping pager. An object matches when at least one nonzero
character in its hotspot-relative graphic intersects either character cell of
the selected 2x2-character tile. Its hotspot may be on another tile.

`platform_look_exit()` applies the same visibility and light-range check to the
current edge tile. An enabled exit always displays the generic `An exit.` -
custom per-exit descriptions (`room->north_text` etc.) aren't wired up yet;
that's a planned extension on top of the room-script mechanism
(`game_room_script_entry()` - see `ROOM_CODE_API.md`'s "Room scripts"). A
disabled direction reports `There is no exit that way.`. Looking outward
never loads the adjacent room.

`platform_object_intersects_tile()` exposes the same rendered-footprint test
for commands such as Take. Transparent object characters do not count.
`platform_object_take_prompt()` renders the selected type's build-prepared
PETSCII name through the bottom pager. `platform_object_taken_message()`
renders the same name followed by `" taken."`.

## Look cursor

```c
uint8_t platform_look_cursor_show(uint8_t tile_x, uint8_t tile_y);
uint8_t platform_look_cursor_move(uint8_t tile_x, uint8_t tile_y);
void platform_look_cursor_tick(void);
void platform_look_cursor_hide(void);
```

Pressing `L` starts the cursor on the player's hotspot tile. Cursor keys move
within the 20x11 room, Return selects, and `L` cancels. Pushing outward while
the cursor is already on an edge displays that direction's exit description
without moving the cursor. The cursor is an 18x18
one-pixel monochrome frame in sprite slot 0, positioned one pixel outside the
selected 16x16 tile. `tick()` cycles black, dark gray, gray, light gray, white,
and back through the grays. Only sprite-0 bits in shared VIC registers are
changed; sprites 1-7 may simultaneously carry the rain layer (or, if a
portrait is shown, sprites 1-5 carry that instead - see below).

Pressing `T` uses the same cursor but clips it to the 3x3 tile neighborhood
centered on the player's hotspot, including the tile underfoot. Return applies
the same LOS/light gate as Look. Actor objects and objects flagged
`PLATFORM_OBJECT_FLAG_NOT_TAKEABLE` never appear as take candidates. If
several takeable object footprints overlap the framed tile, the bottom
display shows one name at a time; cursor keys cycle the choices, Return
takes the displayed object, and `T` cancels. If the tile has no takeable
object but does have a non-actor object that is flagged not takeable, Return
shows "I cannot take this." instead of "Nothing to take.". Removal still
uses the transactional, save-aware `game_take_object()` path, which also
rejects actors and not-takeable objects defensively.

## Portraits

```c
uint8_t platform_portrait_show(uint8_t portrait_id, uint8_t side);
void platform_portrait_hide(void);
```

Fetches the 256-byte `portrait_id` asset (see
`tools/asset-editor/README.md`) from its fixed EasyFlash ROML bank (8 KiB
mode) and displays it using sprites 1-5: sprites 1-4 hold the four 24x21 quadrants
copied directly from the asset (it is exactly their memory layout), and
sprite 5 is filled with a constant solid bitmap, colored black, and expanded
2x horizontally and vertically to form a 48x42 backdrop exactly matching the
2x2 grid's footprint, so transparent portrait pixels show as black rather
than the room behind it. `side` is `PLATFORM_PORTRAIT_LEFT` or
`PLATFORM_PORTRAIT_RIGHT`; the portrait rests 16px from the top and from
that side's edge of the 20x11 room. `platform_portrait_show()` blocks while
it slides the sprite group down from off-screen to its resting position (a
few frames); `platform_portrait_hide()` disables sprites 1-5 immediately,
with no animation. Sprite 0 (look/take/use cursor) is untouched by both
calls, so a shown portrait and the look cursor can coexist. Sprites 1-5 are
also rain's (see below): if rain is active when a portrait is shown,
`platform_portrait_show()` pauses it first, and `platform_portrait_hide()`
resumes it - callers do not need to coordinate this themselves. Loading a
new portrait or hiding the current one does not restore whatever a previous
portrait's sprite data looked like; each `show()` call fully repopulates
sprites 1-5 from the requested asset.

## Rain weather layer

```c
void __fastcall__ platform_rain_enable(void (*setup)(void));
void platform_rain_activate(void);
void platform_rain_disable(void);
uint8_t platform_rain_is_active(void);
```

Rain is an optional room effect: seven independent streaks, each a single
static 45-degree dark-blue diagonal line, each owning one dedicated hardware
sprite (1-7) - not multiplexed, since there are exactly as many rain sprites
as raindrops. Every frame each streak moves 20px right and 20px down; when it
leaves the screen it respawns at a random position along the top or left
edge. The effect never rewrites screen RAM, Color RAM, lighting, or
visibility buffers.

Because sprites 1-5 are shared with the portrait API above, rain and a shown
portrait are mutually exclusive - `platform_portrait_show()`/`hide()` handle
pausing and resuming rain automatically (see Portraits). `game_enter_room()`
disables rain before dispatching the destination room's `enter_room()`, so
whether a room ever shows rain is purely that room's own code's decision -
there is no data-driven "this room is rainy" flag anywhere else (not in the
room file, not in the DSL). A rainy room opts in explicitly, from its own
`enter_room()`:

```c
static void rain_setup(void) { /* sprite pointer/color/VIC-attribute pokes */ }
void enter_room(void) {
    platform_rain_enable(rain_setup);
}
```

`setup` is a one-time callback that pokes the sprite pointer, color, and
VIC-attribute registers for sprites 1-7 (see `rooms/00.c`); it is not part
of the always-resident code because it only ever needs to run synchronously
from room entry or the portrait-resume path, both guaranteed to still have
the calling room's own EasyFlash bank paged in. Pass `0` to reuse whichever
setup was last registered instead of supplying a new one - this is what the
portrait-resume path does, since it never has a setup of its own to give.
`platform_rain_activate()` is the lower-level "just (re)seed and turn the
streaks on" step `platform_rain_enable()` performs after running `setup`;
callers outside the portrait-resume path normally want
`platform_rain_enable()`, not this directly.

`platform_rain_is_active()` reports whether rain is currently on; it exists
for callers that need to pause and later resume rain without assuming its
prior state (the portrait code goes through `env_disable()`/`env_enable()`
instead - see "Room environment module" below - rather than calling this
directly).

The shared streak bitmap occupies `$3B80-$3BBF`; all seven sprite pointers
point at it (`$EE`), since the bitmap is static and only sprite position
changes. The otherwise-unused slot-7 bytes at `$3BC0-$3BFF` hold the compact
per-frame advance/respawn code.

EasyFlash storage reserves banks 49-56 (8 KiB ROML mode, 32 portraits per
bank) for the full 256-ID range, mirroring the room asset layout's fixed
`bank = first_bank + id / per_bank` formula. See
`EASYFLASH_CARTRIDGE.md` for the complete bank table.

## Room environment module

A room's environment module unifies its weather (the rain sprites above)
and ambient sound (a SID rain-bed/droplet/footstep routine) behind one
small jump-table API, refreshed per room the same way room code is:

```
offset 0  JMP env_init     - once, on room entry
offset 3  JMP env_tick     - once per frame, from the raster IRQ
offset 6  JMP env_enable   - resume after a pause
offset 9  JMP env_disable  - pause (e.g. a portrait/conversation)
```

This is reserved, always-resident RAM (`ENVCODE_BASE`, `$7E00-$7FFF` -
`src/platform.inc`/`cfg/myc64.cfg`), never a banked `$A4E9`-style overlay:
the raster IRQ calls `env_tick` every frame via `weather_animate`
(`src/irq.s`), and interrupt code can never bank-switch to reach a room's
own EasyFlash bank to fetch it fresh. Instead, the *whole module* - init,
tick, enable, disable, and whatever persistent state it needs - is copied
into this one fixed, non-banked address range at room entry, and the
existing raster IRQ, `game_enter_room()`, and `platform_portrait_show()`/
`_hide()` all call fixed addresses within it, regardless of which room's
module (if any) is currently loaded there.

`game_enter_room()` (`src/game.c`) fetches the new room's module via
`platform_resource_fetch(PLATFORM_RESOURCE_KIND_ENVIRONMENT, room_id,
ENVCODE_BASE, ENVCODE_SIZE)` - a normal generic resource fetch (see
"Generic cartridge resources" below), keyed by room ID like
`PLATFORM_RESOURCE_KIND_ROOM`. A room with no module of its own gets
`env_install_null()` instead: a stub whose four vectors all just `rts`, so
every call site can call unconditionally with no "is anything loaded"
check. The fetch and the two init calls are bracketed by
`raster_irq_suspend()`/`_resume()` - not just the usual safety margin: the
fetch's own post-copy checksum verification re-enables interrupts before
it finishes, and without this bracket the raster IRQ could fire mid-fetch
and run the *just-copied* module's tick, which mutates its own persistent-
state bytes - changing the very bytes still being checksummed and
spuriously failing the fetch. Found live in VICE, not by inspection: room
00's module kept silently reverting to the null stub even though the copy
and checksum were each independently correct.

`env_enable()`/`env_disable()` (`src/env_dispatch.s`, thin `jmp` trampolines
so C code can call them like ordinary functions) are what
`platform_portrait_show()`/`_hide()` call to pause and resume a room's
environment unconditionally, replacing what used to be a portrait-specific
`platform_rain_is_active()`/`_disable()`/`_enable(0)` dance. A room with no
module simply no-ops.

`rooms/env/<ID>.s` is hand-written ca65 (see `rooms/env/00.s`), not a DSL:
VIC/SID register poking has no mechanical pattern worth one, the same
reasoning that made room code's own `asm` escape hatch necessary before
this. It's built like room code (assembled, resolved against the resident
image via `tools/generate_room_resolver.py`, linked) but at the fixed
`ENVCODE_BASE` origin (`cfg/env_module.cfg`) with no header-patching step -
the generic resource directory already checksums and size-validates the
linked output, so it *is* the final resource content directly.

## Generic cartridge resources

```c
#define PLATFORM_RESOURCE_MAX_BYTES 0x2000u
#define PLATFORM_RESOURCE_KIND_SCRIPT       0u
#define PLATFORM_RESOURCE_KIND_CONVERSATION 1u
#define PLATFORM_RESOURCE_KIND_ROOM         2u
#define PLATFORM_RESOURCE_KIND_ENVIRONMENT  3u
#define PLATFORM_RESOURCE_KIND_ASSET        4u
uint16_t platform_resource_fetch(uint8_t kind, uint8_t resource_id,
                                  uint8_t* destination, uint16_t capacity);
uint16_t platform_resource_fetch_range(uint8_t kind, uint8_t resource_id,
                                       uint16_t start, uint8_t* destination,
                                       uint16_t capacity);
uint16_t platform_resource_last_size(void);
```

Rooms, object types, and portraits all use a fixed-formula bank/offset
because each is fixed-size and fully populated across all 256 IDs. This
call is for content that is not: sparse, variable-size data, e.g. the
room/cutscene/conversation scripts `modules/script.c` interprets (see
`ROOM_CODE_API.md`'s "Room scripts" and `tools/compile_script.py`'s module
docstring). Each `PLATFORM_RESOURCE_KIND_*` has its own independent
256-entry directory reserved in EasyFlash banks 57-63 (see
`EASYFLASH_CARTRIDGE.md`), so a conversation and a standalone script can
both use `resource_id` 5 without colliding. `assets/resources/NN` raw
resources (unstructured bytes, no ABI, unlike room code) share
`PLATFORM_RESOURCE_KIND_SCRIPT`'s directory with compiled cutscenes, since
neither belongs to a room or conversation; both are placed by
`tools/pack_easyflash.py`.

`PLATFORM_RESOURCE_KIND_ASSET` carries the static assets that used to be
`.incbin`'d into the program image - the tile charset, the text charset, and
the tile definitions plus property table as one blob. `platform_init()` fetches
them at boot straight to their reserved addresses, so they cost nothing in the
contiguous image `cart/ef_boot.s` copies into RAM, and a different charset or
tileset becomes a packing decision rather than a rebuild of the engine. Two
consequences worth knowing: the fetches must complete before anything draws
(they run before the raster IRQ is installed), and tile bitmaps and tile
properties ship as a single resource on purpose, because a mismatch between
them means collision silently disagreeing with what is drawn. IDs are
`PLATFORM_ASSET_CHARSET_TILE`, `_CHARSET_TEXT` and `_TILES`.

Note this directory lives at the head of the directory bank's *ROMH* half
rather than its ROML half: four 256-entry directories exactly fill ROML, so
kind 4 and anything added after it continue in ROMH.

`platform_resource_fetch()` copies up to `capacity` bytes from the start of
the resource into `destination` and returns the actual length on success.
Returns `0` if `kind`/`resource_id` is unpopulated, its stored length
exceeds `capacity`, or the copied bytes fail the directory's stored
checksum -- callers should treat `0` as "resource not available" and must
not assume `destination` was left unmodified in that case. A resource
never exceeds `PLATFORM_RESOURCE_MAX_BYTES` (one 8 KiB EasyFlash ROML/ROMH
half), so a fetch is always a single bank selection, never a multi-bank
copy.

`platform_resource_fetch_range()` is for a resource bigger than any single
resident buffer can hold in one piece: it copies up to `capacity` bytes
starting at byte `start` of the resource (not necessarily its beginning)
and returns the number of bytes actually copied (`0` for `start >=` the
resource's size, or a lookup failure). Unlike `platform_resource_fetch()`,
it does not verify the checksum -- that covers the whole resource, not an
arbitrary sub-range, so a corrupt directory entry is still caught, but a
corrupt resource body is not. `platform_resource_last_size()` returns the
size of the resource most recently looked up by either fetch call
(regardless of `kind`, since it just re-reads the shared lookup buffer),
and regardless of how much of it fit in that call's `capacity` -- callers
use it as the upper bound to keep fetching against. `modules/script.c`
uses this pair to keep a sliding window into a script resource up to 8
KiB, re-fetching a fresh window into its much smaller (~1 KiB) resident
buffer whenever a read falls outside the current one.

## Raster IRQ and water animation

The complete raster interrupt implementation is assembly in `src/irq.s`. It
switches from tile charset bank 0 to text charset bank 1 immediately below the
map, restores bank 0 at raster line 0, advances rain once per frame at the
map/text split (an ordinary per-sprite position update, not a multiplexing
event), and acknowledges every VIC interrupt.
Because EasyFlash copies run with interrupts disabled, the handler accepts late
entry and derives the correct phase from `$D011` bit 7 plus `$D012`; the copy
routine explicitly resynchronizes `$D018` and the next compare before restoring
interrupts.

**Contract: interrupt code never bank-switches, and stays on resident data.**
`src/irq.s` never touches `EASYFLASH_BANK`/`EASYFLASH_CONTROL`/`ef_shadow_*`
and never will - the raster IRQ, keyboard polling, and rain/water advance all
run fully resident. This is more than a style preference: most of
`$8000-$BFFF` - resident `BSS`, `platform_room`, the active room-code overlay -
physically sits inside the EasyFlash ROML/ROMH
banking window, so an interrupt that ran while that window was switched to
cart ROM would read garbage there even if its own code never issued a bank
switch itself. Interrupts are therefore kept off for a foreground bank
switch's *entire* switched-in duration, not just around the bank register
writes.

Every foreground (non-interrupt) banked call goes through the general "thin
call wrapper" pair in `src/banking.s`: `_platform_bank_call_enter`/
`_platform_bank_call_leave` (or the `BANK_CALL` macro in `platform.inc`),
bracketing a `jsr` to the banked routine:

```asm
    lda #MY_BANK
    jsr _platform_bank_call_enter
    jsr my_banked_routine
    jsr _platform_bank_call_leave
```

These save/restore the previous bank, EasyFlash mode, CPU memory map, and
interrupt-flag state through a small fixed-depth array (a real bank stack,
not the CPU hardware stack - a `jsr`'d "enter" can't leave state on the
hardware stack for a *different* `jsr`'d "leave" to find, since `rts`
always pops whatever is on top regardless of what pushed it), so nested
banked calls unwind correctly. `easyflash_copy_window` (the primitive behind
`platform_easyflash_copy_roml`/`_romh`) is itself built on this pair.

**Cost of the copy itself, not just the wait beforehand.**
`_platform_bank_call_enter`/`_platform_far_call` waiting for the raster to
wrap before disabling interrupts (documented above and in
`MEMORY_MAP.md`/`MEMORY_MAP_TARGET.md`) is a small, bounded cost - up to
~56 lines, ~3.5ms. The copy loop that runs *after* interrupts are off is a
separate, much larger cost that scales with the amount of data moved:
`easyflash_copy_window`'s byte loop (`src/banking.s`) is roughly 50 6502
cycles per byte, so a multi-KB `platform_overlay_load()` (the copy-to-`$A4E9`
overlays - room-helpers, look-helpers, script/conversation/room-text,
inventory, save/load) can hold interrupts off for well over 100ms, several
full video frames - long enough that the raster IRQ's charset split cannot
run at all for that whole stretch, and the display freezes screen-wide on
whichever single charset was selected the instant interrupts went off
(almost always the tile charset, since the bank-call primitives deliberately
start their critical section at the top of a frame). Any status text already
on screen at that instant renders through the wrong charset as garbled tile
glyphs for a visible fraction of a second, not a one-frame flicker - found
live (`MEMORY_MAP_TARGET.md`'s "Overlay-load visual glitch" section) via
Look/Take/Use, which always have status text ("Looking...", "Taking...",
"Using...") already up when their overlay loads. Bracket a
`platform_overlay_load()` call with `platform_screen_blank()`/`_unblank()`
whenever status text might already be visible - see that section for the
call sites already fixed this way and the ones deliberately left as a
lower-priority follow-up.

### Banked code

Beyond copying *data* out of a bank, code can be linked into the ROML window
and **executed straight from its bank**, never copied into RAM. Call one with
the `FAR_CALL` macro (`src/platform.inc`):

```asm
    FAR_CALL 47, $9A00      ; bank byte + 16-bit offset = a "far address"
```

It patches the bank and target into a single shared trampoline
(`_platform_far_call`, `src/banking.s`) and calls it. The trampoline is
reentrant despite the self-modification - both patched operands are consumed
before its inner `jsr`, and nothing below that `jsr` is patched - so a banked
routine may `FAR_CALL` again. It keeps the caller's bank, mode and map on the
CPU hardware stack, which is why it needs none of the fixed-depth array
`_platform_bank_call_enter`/`_leave` rely on: it brackets the call itself
rather than being two separate `jsr`/`rts` pairs.

Register contract: **`A` and `X` are passed to the callee and its `A`/`X`
return comes back**, which covers cc65's 8- and 16-bit argument and return
passing, so a banked routine can be an ordinary `__fastcall__` C function.
`Y` is the trampoline's scratch and is preserved in neither direction.

**What a banked routine may touch.** While the call runs, `$8000-$BFFF` is
cartridge ROM. Reads there return ROM, not the RAM listed in the table above;
writes still reach the RAM underneath. So a banked routine may only use
`$0000-$7FFF` and `$C000-$CFFF`, and may only call resident code living
outside the window. This is why the software stack (`$C000`) and `GameState`
(`$C100`) were moved - cc65 code touches its stack constantly - and why
`LOWBSS` exists for state the bank machinery itself must read. `UPPERCODE`
(`$8B48`) is inside the window and is therefore unreachable from a bank.

**Granularity.** Both bank-switch paths wait for the raster to wrap before
disabling interrupts: free when the raster is on lines 0-255, up to ~56 lines
otherwise. Negligible for one coarse call wrapping a large piece of work,
costly if the same work is split across many small banked calls. Bank whole
operations, not inner-loop helpers.

`platform_object_type_info_get()` is the first routine to run this way; see
`modules/typeinfo.c`, `cfg/banked_typeinfo.cfg` and the resident stub in
`src/banked_api.s` for the pattern, and `EASYFLASH_CARTRIDGE.md`'s "Modules
executed in place" for the packing side.

The gameplay handler is entered directly through RAM `$FFFE/$FFFF`, saves and
restores A/X/Y, and ends in `RTI`. A second `$0314` entry supports KERNAL-mapped
disk intervals. Keyboard polling uses a short `$37` wrapper and restores the
normal `$35` gameplay mapping before returning.

A contiguous `$C000-$FFFF` object table is possible through `$01` banking, but
records in its `$D000-$DFFF` quarter must be staged through visible scratch RAM
while I/O is temporarily hidden. The renderer cannot update VIC or Color RAM
in that interval. The exact per-ID access policy and the alternative
`$8000-$BFFF` layout are documented in `EASYFLASH_CARTRIDGE.md`.

The bottom-of-map branch also animates tile charset character 14. After the
VIC switches to the text charset, it rotates all eight bytes at `$2070-$2077`
left by one bit, wrapping each byte's bit 7 into bit 0. Water-property tiles 2,
3, and 21 all use character 14 in every quadrant. A one-byte frame divider
runs the rotation every second bottom split: 25 updates/second on PAL and 30
updates/second on NTSC.

## Editor workflows

Room mode has fixed 20x11 dimensions and two tools:

- **Tile paint**: left-drag paints the selected tile; right-drag paints tile 0.
- **Object place**: click an object to select it, click empty space to add the
  selected type, Shift-click to move the selected object's hotspot, and
  right-click an object to delete it.

The editor enforces the 200 non-actor limit and reports total/non-actor counts.

Room mode also has enabled/destination controls for all four neighbor links.
A room's actual content (Look/Use/room-entry/tile-entry text and logic) is
authored separately, as a room script in the editor's Script mode (see
`ROOM_CODE_API.md`), not in Room mode.

The separate Object mode exposes dimensions, hotspot, name, actor flag, and a
visual grid that automatically follows the selected dimensions. It paints
selected characters from charset bank 0 with a per-cell color. Character 0 is
transparent, right-click clears a cell, and the hotspot tool assigns the
anchor by clicking a grid cell. Room mode provides a selected-object form for
changing a room entry's type and half-tile coordinates. Room and object-type
files can be imported/exported locally or opened/saved under `assets/` when
using `make asset-editor`.

## Remaining platform decisions

The following are deliberately not hidden behind incomplete contracts:

- disk save-file and one-block index I/O (see `SAVE_GAME.md`) -- the only save path;
  native EasyFlash EAPI flash-save persistence is deliberately not planned
  (no driver exists, and some emulators need an extra step to persist cartridge
  writes, so cartridge storage stays read-only for game assets);
- the trigger-destination table format;
- collision policy beyond existing tile property bits;
- scheduling and update cadence for NPCs;
- whether transition tables live in room files, a global asset, or game code.

These should be specified before extending the binary formats. The rendering,
object storage, transition detection, and transfer primitives do not depend on
which persistence/transition-table design is chosen.
