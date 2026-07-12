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
| `$3900-$39FF` | compact read-only lookup tables |
| `$3A00-$3BFF` | eight 64-byte sprite bitmap slots |
| `$3C00-$5FFF` | platform code and read-only tables |
| `$6000-$7FFF` | room/work BSS and cc65 software stack |
| `$C000-$FFFF` | 256 resident object-type records |

The platform preallocates:

- one 1,248-byte `PlatformRoom`;
- one byte each for `platform_current_room` and `platform_player_slot`;
- one `PlatformObject* platform_player` pointing into the current room list;
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
void platform_storage_init(PlatformStorage storage, uint8_t device);
void platform_room_state_hooks(PlatformRoomStoreHook store_hook,
                               PlatformRoomRestoreHook restore_hook);
void platform_room_clear(PlatformRoom* room, uint8_t room_id);
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);
uint8_t platform_object_types_load(const char* filename, uint8_t device);
const PlatformObjectType* platform_object_type_get(uint8_t type_id);
uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y);
```

Call `platform_init()` once before other platform APIs. It selects VIC bank 0,
sets border/background black, selects the tile charset, installs the raster IRQ,
and initializes the platform. Room `00` and types 0-1 provide the standalone
PRG fallback; a cartridge loads the complete room/type data from EasyFlash.
These
globals are established:

```c
platform_current_room = 0;
platform_player_slot = 0;
platform_player = &platform_room.objects[0];
```

Thus the player is the actual slot-0 object from room `00`, not a detached
copy. The current asset defines it as actor type 1, "The Hero".

`platform_room_load()` dispatches through `platform_storage`. The disk backend
opens the two-digit filename; the EasyFlash backend selects the fixed ROML bank
and offset. Both read into staging RAM, validate the 1,248-byte record, and only
then commit it to the caller's room.
Loading into the global `platform_room` also updates `platform_current_room`
and rebinds `platform_player` to `platform_player_slot`.
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
    result = platform_room_load(&platform_room, 0x2a);
}
```

### Dynamic-room storage contract

The platform loads rooms throughout gameplay rather than preloading all 256
rooms. The storage-neutral API dispatches the same logical load to a disk or
EasyFlash backend. EasyFlash rooms are immutable base records copied
from banked ROM into the single resident `platform_room`; disk rooms retain the
same `00`-`FF` names and binary format.

A room transition using one resident buffer must follow this order:

1. retain the transitioning player/NPC record outside the room buffer;
2. record modifications to the leaving room in the selected persistence layer;
3. load and validate the destination room;
4. insert the actor into the first valid empty destination slot;
5. update `platform_current_room`, `platform_player_slot`, and
   `platform_player` only after success;
6. draw tiles, objects, and the player.

Because cartridge assets are read-only, moving persistent objects between rooms
requires a mutable delta/save policy. Otherwise returning to a room would reload
its original object list. The current `platform_room_object_transfer()` expects
both rooms in RAM and does not itself solve persistence or the single-buffer
transition case.

The EasyFlash backend, bank layout, RAM copy primitive, and KERNAL/IRQ
constraints are specified in `EASYFLASH_CARTRIDGE.md`.

For disk gameplay, the room files must be present on the generated D64. The
default `make d64` rule adds `res/*`, hexadecimal room files from `assets/`, and
`assets/objects.cobj`. Embedding startup room `00` in the PRG does not make the
remaining rooms available to KERNAL I/O.

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

The native renderer preserves these public functions. Assembly is an
implementation detail: the map blitter streams the 220 tile IDs and writes
screen/Color RAM directly; the object blitter draws one clipped,
transparent object at a time. Room ordering and transition policy remain in C.
Full map/object draws hide an active sprite overlay before invoking the native
blitters. Dirty-cell movement retains its overlay-aware saved-color handling.

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

`platform_player_step()` proposes a signed half-tile delta. The destination
must remain within x `0-39`, y `0-21`, and the tile under the destination
hotspot must have `PLATFORM_TILE_SOLID_LAND` (`$04`) set. Only the hotspot is
tested; the dimensions of the player graphic do not expand the collision
footprint. A permitted step uses `platform_object_move()` and therefore
redraws only changed character/color cells. It returns `PLATFORM_OK` after a
move, `PLATFORM_ERR_BLOCKED` for a boundary/non-land tile, or
`PLATFORM_ERR_ARGUMENT` when no player object is active.

The demo loop first waits for any key event and hides the sprite dialog. It
then handles cursor events as one-half-tile player steps:

```c
do {
    platform_wait_frame();
    key = platform_input_poll();
} while (key == 0);
platform_overlay_hide();

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

The overlay reads its 4x7 glyphs directly from the high nibble of charset bank
1 at `$2800`. The supported ASCII-to-character mapping is:

- `A-Z`: characters 193-218;
- `(`, `$`, `)`, `-`: characters 219-222;
- `a-z`: characters 225-250;
- `.`, `,`, `!`, `?`, `:`: characters 251-255.

Space and unsupported bytes render blank. Each glyph uses rows 0-6 and bits
7-4 of its 8x8 charset character; row 7 and the low nibble are ignored.

### Overlay ASCII lookup table

`ascii_glyph` in `src/overlay.s` is a direct 128-byte lookup indexed by ASCII
code. Each value is the charset-bank-1 character position, or `00` for a blank
or currently unsupported character. This is the complete current table;
columns are the low hexadecimal nibble:

```text
       0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
$00:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
$10:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
$20:  00 FD 00 00 DC 00 00 00 DB DD 00 00 FC DE FB 00
$30:  00 00 00 00 00 00 00 00 00 00 FF 00 00 00 00 FE
$40:  00 C1 C2 C3 C4 C5 C6 C7 C8 C9 CA CB CC CD CE CF
$50:  D0 D1 D2 D3 D4 D5 D6 D7 D8 D9 DA 00 00 00 00 00
$60:  00 E1 E2 E3 E4 E5 E6 E7 E8 E9 EA EB EC ED EE EF
$70:  F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA 00 00 00 00 00
```

The existing named mappings are:

| ASCII | Charset positions |
|---|---|
| `A-Z` | `$C1-$DA` (193-218) |
| `(`, `$`, `)`, `-` | `$DB-$DE` (219-222) |
| `a-z` | `$E1-$FA` (225-250) |
| `.`, `,`, `!`, `?`, `:` | `$FB-$FF` (251-255) |

To add a missing character, draw its seven 4-pixel rows in bits 7-4 of an
available bank-1 charset position, leave row 7 unused, then replace that ASCII
entry's `00` in `ascii_glyph`. For example, ASCII `0` is table index `$30`.
The table deliberately does not infer PETSCII or screen-code conversions.

The packed renderer is implemented in `src/overlay.s`. Sprite RAM is cleared
before the sprites are enabled. The cleared sprites are then positioned and
enabled before characters are drawn, so text visibly crawls into the box.
For every pixel row, an even character ORs its existing high nibble directly
into the destination byte; the following odd character shifts its high nibble
right by four and ORs it into the same byte. Six characters fill the three
bytes of one sprite row. The next six continue at the next sprite's 64-byte
block, and each successive glyph row advances three bytes within that block.

## Raster IRQ and water animation

The complete raster interrupt implementation is assembly in `src/irq.s`. It
switches from tile charset bank 0 to text charset bank 1 immediately below the
map, restores bank 0 at raster line 0, and acknowledges the VIC interrupt.

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

- persistent storage for modified room object lists across 256 rooms;
- the edge-neighbor and trigger-destination table format;
- collision policy beyond existing tile property bits;
- scheduling and update cadence for NPCs;
- flash-save integration and failure recovery;
- whether transition tables live in room files, a global asset, or game code.

These should be specified before extending the binary formats. The rendering,
object storage, transition detection, and transfer primitives do not depend on
which persistence/transition-table design is chosen.
