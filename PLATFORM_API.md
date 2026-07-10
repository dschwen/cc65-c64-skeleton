# C64 Game Platform API

The public runtime API is declared in `src/platform.h` and implemented in
`src/platform.c`. It provides fixed-size room storage, tile and object
rendering, minimal object movement redraws, room-transition detection, bottom
status text, and a sprite-based dialog overlay without dynamic allocation.

## Coordinate systems

The platform uses three related coordinate systems:

| Unit | Range | Purpose |
|---|---|---|
| tile | x `0-19`, y `0-10` | room maps and 2x2-character tiles |
| half-tile / character | x `0-39`, y `0-21` | objects, movement, hotspots, overlays |
| pixel | 320x200 display | sprite hardware and glyph rendering only |

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
| `$3A00-$3BFF` | eight 64-byte sprite bitmap slots |
| `$3C00-$5FFF` | platform code and read-only tables |
| `$6000+` | BSS: room, object types, work buffers |

The platform preallocates:

- one 1,248-byte `PlatformRoom`;
- one three-byte `platform_player` convenience record;
- 256 object-type records, 16 KiB total;
- a 110-byte dirty-cell bitmap;
- 72 saved Color RAM bytes for the overlay.

No API allocates heap memory.

## Room file format

Room files are named by the uppercase two-digit hexadecimal room ID: `00`
through `FF`. Each file is exactly 1,248 bytes.

| Offset | Size | Content |
|---:|---:|---|
| `0` | 1 | width, always 20 |
| `1` | 1 | height, always 11 |
| `2` | 1 | room ID |
| `3` | 1 | format version, currently 1 |
| `4` | 220 | tile IDs, 20x11 row-major |
| `224` | 768 | 256 three-byte object slots |
| `992` | 256 | zero-terminated room-text pool |

An object slot is:

| Byte | Meaning |
|---:|---|
| 0 | object type ID; 0 means empty |
| 1 | hotspot x in half-tiles |
| 2 | hotspot y in half-tiles |

Text strings are addressed by their byte offset in the 256-byte text pool.
Offset 0 is conventionally kept as a zero byte so callers can select an empty
line without a separate sentinel value.

The editor can import the old 224-byte 20x11 map format. It initializes the
new object and text regions to zero and exports the full 1,248-byte format.

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
| 49 | 15 | reserved, must be preserved |

Width and height are each limited to 1-15, and `width * height` must not
exceed 16. Only the first `width * height` character/color entries are used.

The 16 color bytes resolve an ambiguity in the original proposed record: an
object graphic needs both a full eight-bit character code and a color. They
occupy half of the originally proposed 32 future bytes, leaving 16 reserved
bytes including the flags byte.

## Initialization and loading

```c
void platform_init(void);
void platform_room_clear(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id,
                           uint8_t device);
uint8_t platform_object_types_load(const char* filename, uint8_t device);
```

Call `platform_init()` once before other platform APIs. It selects VIC bank 0,
sets border/background black, selects the tile charset, clears overlay sprite
RAM, resets global storage, and installs the raster IRQ.

`platform_room_load()` opens the two-digit room filename as a sequential file,
reads exactly 1,248 bytes, then validates dimensions, ID, and version.
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
result = platform_object_types_load("OBJECTS.COBJ", 8);
if (result == PLATFORM_OK) {
    result = platform_room_load(&platform_room, 0x2a, 8);
}
```

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
Color RAM is changed for that object cell. Objects are clipped to the 40x22
map area.

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

1. mark nontransparent cells at the old position in a 40x22 bitset;
2. update the hotspot position;
3. mark nontransparent cells at the new position;
4. for each marked cell, compose tile + ordered objects + player;
5. compare the result with current screen/color RAM;
6. write only bytes that differ.

This correctly restores exposed tiles and overlapping objects without
redrawing the room or blindly writing the entire bounding rectangle.

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

Destination room IDs and arrival coordinates are intentionally not stored in
the current room format because no neighbor/trigger-table binary schema has
been selected yet. Game code should resolve:

```text
(current room, edge direction)
or
(current room, trigger tile/position)
```

to a destination room and hotspot, load the destination, call
`platform_room_object_transfer()` for a listed player/NPC, then redraw. This
keeps the current room contract stable until the transition table requirements
are explicit.

## Bottom text API

```c
uint8_t platform_text_screen_code(char ch);
void platform_text_clear_line(uint8_t line);
void platform_text_write_line(uint8_t line, uint8_t column,
                              const char* text, uint8_t color);
void platform_text_write_room_line(const PlatformRoom* room, uint8_t line,
                                   uint8_t column, uint8_t text_offset,
                                   uint8_t color);
```

Line 0 is screen row 23; line 1 is row 24. Strings are clipped at column 40.
The raster IRQ has already selected charset bank 1 for these rows.

The editor's text charset convention is preserved:

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

## Sprite dialog overlay

```c
uint8_t platform_overlay_show(const PlatformRoom* room,
                              uint8_t half_x, uint8_t half_y,
                              uint8_t line0_offset,
                              uint8_t line1_offset,
                              uint8_t line2_offset,
                              uint8_t sprite_color);
void platform_overlay_hide(void);
uint8_t platform_overlay_is_visible(void);
extern const uint8_t platform_overlay_gray[16];
```

The overlay uses all eight standard-resolution monochrome sprites side by
side. It is 192x21 pixels and renders three lines of 48 4x7 glyphs. Each line
parameter is a byte offset into `room->text`; point a line at offset 0 to leave
it empty.

`half_x` and `half_y` align the overlay to the 8x8 character grid. Valid
origins are x `0-16` and y `0-19`. The sprite top is shifted down one pixel,
leaving the 21-pixel box visually centered over the three underlying 8-pixel
character rows.

The platform saves and darkens the 24x3 Color RAM cells behind the sprites.
The precomputed lookup table is:

```c
{ 0,12,11,12,11,11,0,12,11,11,11,0,11,12,11,11 }
```

Only existing C64 colors are possible in Color RAM, so the mapping uses black,
dark gray, and gray as darker desaturated approximations. While the overlay is
visible, map/object redraws update the saved original color and leave the
visible cell darkened. `platform_overlay_hide()` restores the latest originals.

The embedded font supports ASCII 32-95; lowercase is folded to uppercase.
Unsupported bytes render as `?`.

## Editor workflows

Map mode has fixed 20x11 dimensions and two tools:

- **Tile paint**: left-drag paints the selected tile; right-drag paints tile 0.
- **Object place**: click an object to select it, click empty space to add the
  selected type, Shift-click to move the selected object's hotspot, and
  right-click an object to delete it.

The editor enforces the 200 non-actor limit and reports total/non-actor counts.
The room text editor accepts one string per line and displays the generated
hexadecimal offsets used by the C API.

The object-type editor exposes dimensions, hotspot, name, actor flag, and all
16 character/color cells. A character value of 0 is transparent. Room and
object-type files can be imported/exported locally or opened/saved under
`assets/` when using `make asset-editor`.

## Remaining platform decisions

The following are deliberately not hidden behind incomplete contracts:

- persistent storage for modified room object lists across 256 rooms;
- the edge-neighbor and trigger-destination table format;
- collision policy beyond existing tile property bits;
- scheduling and update cadence for NPCs;
- flash-save integration and failure recovery;
- whether transition tables live in room files, a global asset, or game code.

These should be specified before extending the binary formats. The rendering,
object storage, transition detection, and transfer primitives do not depend on
which persistence/transition-table design is chosen.
