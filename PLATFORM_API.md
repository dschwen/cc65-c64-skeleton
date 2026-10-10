# C64 Game Platform API

The public runtime API is declared in `src/platform.h` and implemented in
the C modules in `src/` and banked services in `modules/`. It provides fixed-size room storage, tile and object
rendering, minimal object movement redraws, room-transition detection, bottom
status text, lighting/visibility, and a sprite tile cursor without dynamic
allocation.

See `MEMORY_MAP.md` for memory ownership and `build/game.map` for exact
compiler-allocated addresses. `GUIDE_OSCAR64_C64.md` summarizes the compiler.

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
| `$0800-$08FF` | Oscar64 startup |
| `$0900-$7DFF` | resident code/data/BSS, software stack and static frames |
| `$7E00-$7FFF` | relocated environment module |
| `$8000-$BFFF` | selected EasyFlash ROM bank |
| `$C000-$CFFE` | hot object records 106-222, explicitly loaded at boot |
| `$D000-$DFFF` | I/O and Color RAM |
| `$E800-$EFFF` | tile charset, explicitly loaded from bank 15 |
| `$F000-$F7FF` | text charset, explicitly loaded from bank 15 |
| `$F800-$FBFF` | screen matrix and sprite pointers |
| `$FC00-$FDFF` | cursor/portrait/rain sprite data |

Oscar64's CRT loader initializes only `main`. CIA/KERNAL/editor setup
and the high-RAM asset copies are explicit in `platform_init()`.
Gameplay keeps KERNAL visible (`$01=$37`); CPU reads of VIC RAM use
a short resident mapping gate. `main` ends before the environment window,
including both stacks. See `MEMORY_MAP.md` for the complete current contract.

The platform preallocates:

- one 1,001-byte `PlatformRoom` plus resident staging;
- one byte each for `platform_current_room` and `platform_player_slot`;
- one `PlatformObject* platform_player` pointing into the current room list;
- 256 logical 35-byte hot object-type records: IDs 0-222 in RAM, 223-255
  in ROM with a resident one-record cache; names/flags remain cold ROM data;
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
| `9` | 1 | room lighting level (`north_text` field) |
| `10` | 1 | line-of-sight enabled (`east_text` field) |
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
compiled into bank 32 from DSL source
(`assets/scripts/<hex room id>.script`, `tools/compile_script.py`) and run via
`game_room_script_entry()` (see "Room-code helper API" below and
`ROOM_CODE_API.md`). Offsets 9 and 10 now carry lighting/line-of-sight settings (see Lighting).
Custom exit descriptions are not wired up; `platform_look_exit()` shows a
generic message.

The exit mask is separate from the four neighbor bytes because every byte
value, including room `FF`, is a valid destination. Links may be one-way. The
editor imports legacy, format-1, and format-2 rooms and exports format 3 (any
embedded text those legacy formats carried is dropped on import - see the
asset editor README).

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

This file format is authoring-only. `tools/prepare_c64_assets.py` splits every
64-byte record at build time into the resident `PlatformObjectType` (35
bytes: width/height, hotspot, chars, colors, light) and the cartridge-only
`PlatformObjectTypeInfo` (15 bytes: name, flags); see "Object types: hot/cold
split" below and `EASYFLASH_CARTRIDGE.md`. The 14 padding bytes are dropped
by that split, not preserved anywhere.

## Initialization and loading

```c
void platform_init(void);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id);
uint8_t platform_object_types_load(void);
const PlatformObjectType* platform_object_type_get(uint8_t type_id);
const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id);
uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y);
```

Call `platform_init()` once before other APIs. It establishes KERNAL/CIA
keyboard state, selects VIC bank 3, explicitly loads high-RAM assets, loads
room 00 and installs the raster/NMI handlers. The player binds to slot 0 in
the initial room record; there is no disk/PRG fallback runtime.

`platform_room_load()` looks up the compiled room's bank, copies its
1,001-byte record to resident staging, validates dimensions/ID/format/exit
mask, then commits. Loading the global room also updates current-room and
player pointers plus room lighting settings. Missing compiled IDs return
`PLATFORM_ERR_NOT_FOUND`; invalid headers return `PLATFORM_ERR_FORMAT`.

`platform_object_types_load()` initializes zone B; zone A is part of main's
initialized data and zone C remains banked. It returns `PLATFORM_OK`.
See the hot/cold contract below; this is not a disk-read operation.

Example:

```c
uint8_t result;

platform_init();
result = platform_object_types_load();
if (result == PLATFORM_OK) {
    result = platform_room_load(&platform_room, 0x02);
}
```

### Object types: hot/cold split

The 35-byte `PlatformObjectType` contains dimensions, hotspot, character/color
cells and emitted light. IDs 0-105 are main data; 106-222 live at
`$C000-$CFFE`, explicitly initialized from bank 16; 223-255 remain in bank 16
and are copied into a one-record resident cache on a cache miss.
`platform_object_type_get()` returns the corresponding record. Treat returned
pointers as temporary across later lookups.

Names and flags are separate 15-byte `PlatformObjectTypeInfo` records in
bank 1. `platform_object_type_info_get()` invokes that service through a
resident wrapper and returns resident scratch. Use it for input-paced
Look/Take/Use/actor checks, not per rendered cell. Copy values needed across
subsequent calls. Actor classification uses `info->flags`.

`tools/prepare_c64_assets.py objects-split` creates the two streams;
`src/object_types_data.c` and `modules/typeinfo.c` embed/place them.
See [EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md) for the bank table.

### Dynamic-room storage contract

The platform loads rooms throughout gameplay rather than preloading all 256
rooms. Rooms are immutable base records copied from banked EasyFlash ROM into
the single resident `platform_room`. All game-asset reads (rooms, object
types, portraits, the in-place Inventory service, room code) are EasyFlash-only;
disk KERNAL I/O is reserved for the implemented save-game backend
(see `SAVE_GAME.md`; disk round trips are not covered by `make test`).

A room transition using one resident buffer follows this order:

1. clear rows 22-24, disable the raster source, and force tile charset bank 0;
2. retain the transitioning player/NPC record outside the room buffer;
3. load and validate the destination immutable room;
4. remove its baked startup-player record when applicable;
5. collision-check the arrival tile and select the destination room handler entry;
6. capture modifications to the leaving room while its baseline is current;
7. preflight an actor slot, establish the destination baseline, and apply deltas;
8. insert the actor into the first valid empty slot;
9. update `platform_current_room`, `platform_player_slot`, and
   `platform_player` only after success;
10. activate its handler bank, load/init its environment, invoke entry hooks;
    reveal/redraw the room through the game transition helpers;
11. resynchronize the split and reenable the raster source.

Every failure after step 1 follows the same resume path, so an I/O, format,
collision, room-code, or capacity error cannot leave the raster IRQ disabled.

`src/world.c` supplies that mutable policy with exact-slot deltas against the
immutable asset. See `SAVE_GAME.md` for invariants and capacity.

The EasyFlash backend, bank layout, RAM copy primitive, and KERNAL/IRQ
constraints are specified in `EASYFLASH_CARTRIDGE.md`.
Room-specific handlers, `GameState`, and the room-code ABI are specified in
`ROOM_CODE_API.md`.

## Map and room drawing

```c
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
extern uint8_t platform_room_light_level;
extern uint8_t platform_room_los_enabled;
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

### Per-room lighting level and line-of-sight

Each room's `north_text`/`east_text` header bytes (see the `PlatformRoom` doc
comment in `platform.h`) double as two performance settings, cached by
`room_commit()` into `platform_room_light_level`/`platform_room_los_enabled`
every time that room becomes active (boot or transition):

- **Light level** (`north_text`, 0-4): 0-3 is one of the `PLATFORM_LIGHT_*`
  levels and is applied as the room's ambient floor (`platform_global_light`)
  on entry - the normal per-object dynamic lighting calculation still runs
  every draw/move after that, and `+`/`-` still adjust it live. 4 means full
  brightness with the entire dynamic-lighting rebuild (wall-cache prep, every
  light-emitting object's raycast) skipped every time - `platform_global_light`
  is ignored while a level-4 room is current. tools/asset-editor's Room Editor
  panel exposes this as "Light level"; existing rooms default to 4 (matching
  the engine's original always-full-dynamic-off-by-default behavior for
  rooms authored before this setting existed).
- **Line-of-sight enabled** (`east_text`, 0/1): 0 skips the line-of-sight
  raycast entirely and marks every tile visible (the same fallback
  `view_rebuild()` already used when there's no valid player); nonzero (the
  default) leaves it enabled. Exposed as "Line-of-sight calculation enabled"
  in the same panel.

Both are cheap enough to leave enabled for most rooms; use them for rooms
that don't need per-object dynamic lighting or hidden-tile occlusion (e.g. a
single bright open room) to skip that room's `visibility_build()`/
`platform_lighting_rebuild()` cost on every draw and object move.

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

void platform_player_set_type(uint8_t type_id);

uint8_t platform_room_object_add(PlatformRoom* room, uint8_t type,
                                 uint8_t x, uint8_t y, uint8_t* out_slot);
uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player);
uint16_t platform_room_object_count(const PlatformRoom* room,
                                    uint8_t actor_only);
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

`platform_player_set_type()` changes the player's own object type in place
(e.g. a terrain-driven cosmetic swap, like room 01's water tiles) and
repaints at the player's current position. Unlike `platform_object_move()`,
it marks the *old* type's footprint before changing `.type` and the *new*
type's footprint after, so the two types need not share dimensions,
hotspot, or transparent-cell pattern. No-op if the player is already
`type_id`.

`platform_room_object_add()` uses the first type-0 slot. It rejects the add
when all 256 slots are occupied or when the room already has 200 non-actor
objects. Actor types are identified by `PLATFORM_OBJECT_FLAG_ACTOR` in the
object-type flags byte. `out_slot` may be `NULL`.

`platform_room_object_count()` accepts:

- `0`: all populated objects;
- `1`: actors only;
- `2`: non-actors only.

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

`platform_frame_counter` cannot advance while the raster source is suspended.
`platform_wait_frame()` then polls the VIC raster directly as a fallback;
it does not service weather/environment ticks on that path. Normal waits
consume the pending foreground weather tick after the frame boundary.
Keep blocking interaction outside suspended transition work when possible.

The raster IRQ does not run the normal KERNAL keyboard handler.
`platform_input_poll()` in `src/input.c` masks IRQs, calls SCNKEY then GETIN,
captures the byte through an Oscar64 assembly expression, resynchronizes
the next raster event, and restores processor status. Call it once after each
frame wait; an unthrottled loop advances debounce/repeat too quickly.
It returns zero when no event is available.

Cursor-key values are exposed as:

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

Format 3 stores four edge destinations; header bytes 9/10 are lighting settings.
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
and ordinary edge-of-room walking, both queued through
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
whose helper block loads at `$3A00`; the pager entry remains `$3A73`.

`platform_text_screen_enter()` tells the assembly raster IRQ to keep charset
bank 1 selected at raster line zero, making all 25 rows text rows.
`platform_text_screen_leave()` restores the normal tile-map/text-line split.
These calls only select the charset; a full-screen UI owns clearing, drawing,
and restoring screen and Color RAM while the mode is active.

For C string literals, the text charset convention is:

- lowercase ASCII `a-z` maps to screen codes 1-26;
- uppercase ASCII `A-Z` maps to screen codes 65-90;
- other bytes pass through unchanged.

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
custom per-exit descriptions (`room->west_text`/`south_text`) aren't wired up yet;
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

The Makefile discovers `assets/portraits/<id>` (256 bytes each - see
`tools/asset-editor/README.md`) and embeds them into `BANK_PORTRAIT_FIRST`
(src/banks.h) the same way as room scripts/conversations - one shared bank,
array-indexed dispatch, reached through the generic resource fetch
(`PLATFORM_RESOURCE_KIND_PORTRAIT`, src/resources.c). `make test` covers it
(Sir Caldus's portrait, room 02, checked byte-for-byte against the asset).
The API fetches the 256-byte `portrait_id` asset and displays it using
sprites 1-5: sprites 1-4 hold the four 24x21 quadrants
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
void platform_rain_enable(void (*setup)(void));
void platform_rain_activate(void);
void platform_rain_disable(void);
```

Rain is an optional room effect: seven independent streaks, each a single
static 45-degree dark-blue diagonal line, each owning one dedicated hardware
sprite (1-7) - not multiplexed, since there are exactly as many rain sprites
as raindrops. Every frame each streak moves 20px right and 20px down; when it
leaves the screen it respawns at a random position along the top or left
edge. The effect never rewrites screen RAM, Color RAM, lighting, or
visibility buffers.

Sprites 1-5 are shared with portraits. The portrait API pauses/resumes the
current environment through `env_disable()`/`env_enable()`.
`game_enter_room()` first disables rain, then selects and initializes the
environment from `rooms/env_banks.h`. Rooms 00/01 use the rain environment.

`platform_rain_enable(setup)` registers/runs a sprite-setup callback and
activates the streaks. Passing zero reuses the saved callback; it must remain
callable under the caller's mapping. The current rain callback lives in the
relocated environment, not in a bank that may have been switched out.
`platform_rain_activate()` reseeds/enables movement;
`platform_rain_disable()` clears the active state and sprite mask.

The shared streak bitmap is `$FD80-$FDBF`, sprite pointer `$F6` in VIC bank 3.
Water/rain initialization code is compiler-placed resident code.
`$FDC0-$FDFF` is the remaining unused sprite-data slot.

## Room environment module

Environment modules in `rooms/env/*.c` combine ambient SID sound and optional
weather. Each is linked for `$7E00-$7FFF` and copied from its own ROM bank
on room entry. `rooms/env_banks.h` selects the bank; this does not use the
generic resource-kind loader.

The first eight bytes are an `EnvTable`:

| Offset | Native function pointer |
|---:|---|
| 0 | init |
| 2 | tick |
| 4 | enable |
| 6 | disable |

A dedicated entry section precedes every constant/code section. These are
two-byte addresses, not three-byte JMP instructions. Keep the whole relocated
module within 512 bytes and mutable globals resident. The null module points
all four slots at a resident no-op.

`game_enter_room()` copies the module and calls init within the room-switch
raster-suspension bracket. `platform_wait_frame()` services its tick from
foreground code; IRQ only marks work pending. `src/env_dispatch.c` supplies
the init/enable/disable wrappers used by the platform and portrait paths.
Replacing the module must not leave a pending call into old entry addresses.

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

`PLATFORM_RESOURCE_KIND_ROOM` and `PLATFORM_RESOURCE_KIND_CONVERSATION` are
implemented in `src/resources.c`, which dispatches to a separate bank per
kind (independent 0-255 ID spaces - src/banks.h's `BANK_SCRIPT_ROOM_FIRST`/
`BANK_SCRIPT_CONVERSATION_FIRST`). The Makefile compiles `assets/scripts/
00.script` through `05.script` into bank 32 and `assets/scripts/
conversations/*.script` into its own bank; `tools/generate_script_bank.py`
emits each bank's arrays and dispatcher. The remaining kind constants
reserve interfaces; they do not imply content is built or retrievable.
Environments, charsets/tiles, rooms and object records use separate direct
paths. There is no generic directory/checksum packer.

`platform_resource_fetch_range()` copies a window starting at `start` into
resident destination storage and returns the bytes copied (zero if absent or
out of range). `platform_resource_last_size()` reports the total looked-up
resource size. `platform_resource_fetch()` starts at zero and returns zero
if the complete resource does not fit; a partial copy may already have changed
the destination. No runtime resource-body checksum is performed.

`modules/script.c` uses the range API for its sliding resident bytecode window.
Do not retain a pointer to bank-local bytecode after restoring the caller bank.

## Raster IRQ and water animation

The raster implementation is in `src/irq.c`. It selects tile charset
`$D018=$EA` at raster zero and text charset `$EC` at line 227
(triggered at line 226), increments the frame counter and marks weather work
pending. Full-screen menus retain text throughout, including late-entry and
input-resynchronization paths.

The IRQ body never switches banks. The normal KERNAL-visible path enters via
`$0314`, uses an Oscar64 `__interrupt` body and the KERNAL restore exit.
The RAM-vector path uses `__hwinterrupt` for CPU-register save/restore and RTI.
NMI has its own hardware wrapper and CIA2 acknowledgment. Do not interchange
these entry contracts.

Water, rain movement and the environment tick run in
`platform_weather_service()` from the foreground frame wait. Water rotates
an eight-byte resident shadow of tile character 14 every other serviced frame,
then writes it to `$E870-$E877`. Initial shadow capture uses
`platform_copy_vic_ram()`; CPU loads with KERNAL visible would read ROM.
The IRQ itself hides rain sprites for bottom text and restores them at the top.

### Banked code

Use `far_call_bank` in `src/memmap.h` and `__noinline` banked entries.
It saves/restores `platform_cart_bank`, the software shadow of write-only
`$DE00`. Request/result adapters live in resident memory. Nested calls restore
the caller's bank; a native pointer does not encode the bank.

Code/constants can be read inside the selected bank, but resident state and
both compiler stacks must be outside `$8000-$BFFF`. Cross-bank calls and
CPU-mapping changes must go through resident code. Keep KERNAL/cartridge
visible while running banked instructions; only the resident RAM gate briefly
clears HIRAM for high-RAM reads.

No ROM self-modification, custom register ABI, packed service entry address
or separate resolver is required. Inspect section pragmas and `build/game.map`
for actual placement. Save/load services use banks 6/7 and disk I/O bank 14;
their borrowed tile storage is restored before drawing.

See [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) for the compiler model and
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md) for the full bank table.

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
using `python3 tools/asset-editor/server.py --host 127.0.0.1 --port 8000`.

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
