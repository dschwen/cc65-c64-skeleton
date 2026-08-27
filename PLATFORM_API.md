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
| `$8000-$84E8` | current 1,257-byte room RAM |
| `$84E9-$855C` | fixed `GameState` |
| `$855D-$85FF` | compact native resident helpers |
| `$8600-$8B47` | resident world-state code |
| `$8B48-$98FF` | resident game/main and shared room-API code |
| `$9900-$9CFF` | active 1 KiB room-code overlay |
| `$9D00-$9FFF` | pristine current-room object baseline |
| `$A000-$A4E8` | destination-room staging |
| `$A4E9-$B4D8` | rebuildable work RAM; inventory/story overlay while active |
| `$B500-$B80C` | ordinary resident BSS |
| `$B80D-$B9FF` | independently loaded helpers and bottom-text pager |
| `$BA00-$BBFF` | cc65 software stack |
| `$BC00-$BFFF` | sparse room-object delta journal |
| `$C000-$FFFF` | 256 resident object-type records |

The platform preallocates:

- one 1,257-byte `PlatformRoom`;
- one byte each for `platform_current_room` and `platform_player_slot`;
- one `PlatformObject* platform_player` pointing into the current room list;
- 256 object-type records, 16 KiB total;
- a 110-byte dirty-cell bitmap.

No API allocates heap memory.

## Room file format

Room files are named by the uppercase two-digit hexadecimal room ID: `00`
through `FF`. Each file is exactly 1,257 bytes.

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
| `9` | 1 | north exit-description text offset |
| `10` | 1 | east exit-description text offset |
| `11` | 1 | west exit-description text offset |
| `12` | 1 | south exit-description text offset |
| `13` | 220 | tile IDs, 20x11 row-major |
| `233` | 768 | 256 three-byte object slots |
| `1001` | 256 | zero-terminated room-text pool |

An object slot is:

| Byte | Meaning |
|---:|---|
| 0 | object type ID; 0 means empty |
| 1 | hotspot x in half-tiles |
| 2 | hotspot y in half-tiles |

Text strings are addressed by their byte offset in the 256-byte text pool.
Offset 0 is conventionally kept as a zero byte so callers can select an empty
line without a separate sentinel value.

Editor room files store text as ASCII. `tools/prepare_c64_assets.py` converts
the text pool to PETSCII in `build/assets/`; only those prepared copies are
embedded in the PRG or packaged into D64/EasyFlash images.

The exit mask is separate because every byte value, including room `FF`, is a
valid destination. Links may be one-way. A description offset of zero means
"use the generic `an exit.` text". The editor imports legacy, format-1, and
format-2 rooms and exports format 3. Use `tools/migrate_rooms_v2.py` and then
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
| 48 | 1 | flags; bit 0 means PC/NPC actor |
| 49 | 1 | emitted light amount; 0 means no light |
| 50 | 14 | reserved, must be preserved |

The editor stores names as ASCII. The same build preparation step converts the
14-byte name fields to PETSCII before the game loads them.

Width and height are each limited to 1-15, and `width * height` must not
exceed 16. Only the first `width * height` character/color entries are used.

The 16 color bytes resolve an ambiguity in the original proposed record: an
object graphic needs both a full eight-bit character code and a color. They
occupy half of the originally proposed 32 future bytes. The remaining 16 bytes
hold flags, emitted light, and 14 bytes reserved for later use.

## Initialization and loading

```c
void platform_init(void);
void platform_room_state_hooks(PlatformRoomStoreHook store_hook,
                               PlatformRoomRestoreHook restore_hook);
void platform_room_clear(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id);
const char* platform_room_exit_description(const PlatformRoom* room,
                                           uint8_t direction);
uint8_t platform_object_types_load(void);
const PlatformObjectType* platform_object_type_get(uint8_t type_id);
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
reads into staging RAM, validates the 1,257-byte record, and only then commits
it to the caller's room. Loading into the global `platform_room` also updates
`platform_current_room` and rebinds `platform_player` to `platform_player_slot`.
`platform_object_types_load()` reads all 16 KiB of the type list. Both return:

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

`platform_lightning()` is bound to `F` in the sample game. Its assembly routine
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

`platform_room_exit_description()` returns the selected zero-terminated room
text string, or `NULL` when the direction is invalid or has no description.
The tile-cursor Look command keeps its frame inside the map; pushing outward at
an edge displays the enabled exit description without loading the neighboring
room.

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
void game_text_write_room(uint8_t line, uint8_t text_offset, uint8_t color);
uint8_t platform_text_screen_code(char ch);
void platform_text_screen_enter(void);
void platform_text_screen_leave(void);
void platform_text_clear_line(uint8_t line);
void platform_text_write_line(uint8_t line, uint8_t column,
                              const char* text, uint8_t color);
void platform_text_write_room_line(const PlatformRoom* room, uint8_t line,
                                   uint8_t column, uint8_t text_offset,
                                   uint8_t color);
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

Game and room code should normally use `game_text_write()` or
`game_text_write_room()`. These clear the status area, wrap at word boundaries,
and split words longer than 40 characters. When output needs a third line, the
pager waits for a fresh press and release, moves the lower line to the upper
line, clears the lower line, and continues. Explicit carriage returns and line
feeds also advance through the same pager. The implementation is an assembly
module whose helper block loads at `$B80D`; the pager entry remains `$B880`.

The lower-level `platform_text_write_line()` and
`platform_text_write_room_line()` calls remain available for fixed-position UI
and clip at column 40; they do not invoke wrapping or paging.

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
platform_text_write_room_line(&platform_room,
                              PLATFORM_TEXT_LINE_BOTTOM,
                              1, room_string_offset, 1);
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
current edge tile. An enabled exit then displays its room-text description, or
`An exit.` when no description is assigned. A disabled direction reports
`There is no exit that way.`. Looking outward never loads the adjacent room.

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
changed; sprites 6-7 remain available to game code. Sprites 1-5 are owned by
the portrait API below.

Pressing `T` uses the same cursor but clips it to the 3x3 tile neighborhood
centered on the player's hotspot, including the tile underfoot. Return applies
the same LOS/light gate as Look. If several non-actor object footprints overlap
the framed tile, the bottom display shows one name at a time; cursor keys cycle
the choices, Return takes the displayed object, and `T` cancels. Removal still
uses the transactional, save-aware `game_take_object()` path.

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
with no animation. Sprite 0 (look/take/use cursor) and sprites 6-7 are
untouched by both calls, so a shown portrait and the look cursor can
coexist. Loading a new portrait or hiding the current one does not restore
whatever a previous portrait's sprite data looked like; each `show()` call
fully repopulates sprites 1-5 from the requested asset.

EasyFlash storage reserves banks 49-56 (8 KiB ROML mode, 32 portraits per
bank) for the full 256-ID range, mirroring the room asset layout's fixed
`bank = first_bank + id / per_bank` formula. See
`EASYFLASH_CARTRIDGE.md` for the complete bank table.

## Raster IRQ and water animation

The complete raster interrupt implementation is assembly in `src/irq.s`. It
switches from tile charset bank 0 to text charset bank 1 immediately below the
map, restores bank 0 at raster line 0, and acknowledges the VIC interrupt.
Because EasyFlash copies run with interrupts disabled, the handler accepts late
entry and derives the correct phase from `$D011` bit 7 plus `$D012`; the copy
routine explicitly resynchronizes `$D018` and the next compare before restoring
interrupts.

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
The room text editor accepts one string per line and displays the generated
hexadecimal offsets used by the C API.

Room mode also has enabled/destination controls for all four neighbor links.

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

- disk A/B save-file I/O (see `SAVE_GAME.md`) — decided as the only save path;
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
