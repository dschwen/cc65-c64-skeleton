# Room-specific code API

Every room asset has one independently linked C overlay:

```text
assets/00  <->  rooms/00.c  <->  build/rooms/C00
assets/7F  <->  rooms/7F.c  <->  build/rooms/C7F
```

`make d64` writes each `CXX` file beside its `XX` room. `make cartridge`
packs the same bytes into EasyFlash ROMH and writes an eight-byte directory
entry for every room. A room asset without a matching source fails the build.

## Required handlers

Every `rooms/XX.c` includes `game.h` and defines exactly these functions:

```c
void enter_room(void);
void enter_tile(void);
uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y);
uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y);
```

`enter_room()` runs once after the destination room data and code are active,
the player has been inserted, and `game_state` has been synchronized. It runs
on initial startup and after a successful room transition, but not while the
player moves between tiles in the same room. Use it for room-wide state changes,
entry narration, and initialization that must happen on every visit.

`enter_tile()` takes no coordinates. Read `game_state.player_x` and
`game_state.player_y`; both are half-tile/character coordinates. The engine
calls it immediately after `enter_room()` and whenever successful movement
changes the player's hotspot tile. A one-character half-step within the same
2x2 tile does not call it.

`look_at()` receives the tile selected by the Look cursor. The resident engine
checks line of sight and usable light range before calling this hook, so a room
description cannot reveal a hidden or insufficiently lit tile. Return
`GAME_LOOK_HANDLED` after producing room-specific output, or
`GAME_LOOK_DEFAULT` to let `platform_look_tile()` list intersecting objects.

`use_at()` receives the tile selected by the adjacent-tile Use cursor after
the same visibility/light check used by Take. Return `GAME_USE_HANDLED` after
performing the complete room-specific action, or `GAME_USE_DEFAULT` to make
the resident command print `Nothing happens.`. Door, lever, container, and
terrain interactions belong here; generic inventory-item use belongs in
`story/story.c` instead.

Example:

```c
#include "game.h"

void enter_room(void) {
    if (game_entry_reason == GAME_ENTRY_STARTUP) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You wake by the sea.", 1u);
    }
}

void enter_tile(void) {
    if ((game_state.player_x >> 1) == 4u &&
        (game_state.player_y >> 1) == 2u) {
        game_state.flags[3] = 1u;
    }
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 4u && tile_y == 2u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "The sea is rough today.", 1u);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}

uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 7u && tile_y == 3u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "The door opens.", 1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
```

## Room-code DSL (.rc files)

Most room code turns out to be one of three mechanical shapes: an empty
handler, an unconditional flag bump on tile entry, or a tile-coordinate
dispatch to a room script. A room can be authored as `rooms/XX.rc` in a
small declarative DSL instead of `rooms/XX.c` - `tools/compile_room.py`
compiles it straight to the same `.s` a hand-written room would produce.
The Makefile picks whichever source file exists (`rooms/XX.c` or
`rooms/XX.rc`) for a given room ID; both forms coexist in this project,
room by room.

```
enter_room: default
enter_tile: flag STORY_STATE_ROOM_01_TILE_ENTRY_COUNT increment

look_at:
    at 18,3 -> script STORY_ROOM00_LOOK_TREE
    default -> GAME_LOOK_DEFAULT

use_at:
    at 18,3 -> script STORY_ROOM00_USE_TREE
    default -> GAME_USE_DEFAULT
```

`enter_room`/`enter_tile` take one line: `default` (empty handler),
`flag NAME increment`, `flag NAME set VALUE`, or `asm "path"` (see escape
hatch below). `look_at`/`use_at` each start an indented block of zero or
more `at X,Y -> script KEY` lines followed by a mandatory
`default -> RETURN_CONST` line. `X`, `Y`, `KEY`, and `RETURN_CONST` may be
decimal/hex literals or `#define` symbols from `src/story.h`/`src/game.h`
(or files passed via `--flags`).

This does not solve a space problem - compiled room overlays are nowhere
near their `$0400` ceiling. The point is authoring simplicity: a room's
flag-bump/dispatch logic becomes one declarative line each, instead of
hand-written (and hand-copied) C.

### Escape hatch: genuinely custom logic

`enter_room: asm "path/to/fragment.s"` (or the same for `enter_tile`)
splices a hand-written `.s` fragment's text verbatim into the generated
output. The fragment supplies its own `.export _enter_room` (or
`_enter_tile`), label, and body - nothing else is generated for that
handler. Use this for logic no DSL statement covers, such as room 00's
`enter_room()` poking VIC-II sprite registers directly for the rain
effect (`rooms/asm/00_enter_room.s`, referenced from `rooms/00.rc`).
Make can't see a `.rc` file's `asm "..."` reference through the Python
compile step, so a room using the escape hatch needs an explicit extra
Makefile prerequisite line (see the one for `room-00.s` in `Makefile`)
so the fragment's own edits trigger a rebuild.

## Global game state

`game_state` is resident at `$C100` and survives overlay replacement:

```c
typedef struct GameState {
    uint32_t turn;
    uint16_t day;
    uint8_t hour, minute;
    uint8_t current_room, player_type, player_x, player_y;
    uint8_t health, maximum_health, mana, maximum_mana;
    uint8_t pending_transition, pending_room, pending_x, pending_y;
    GameInventorySlot inventory[32];
    uint8_t flags[32];
} GameState;
```

The engine synchronizes room, player type, and player coordinates after movement
and room transitions. `turn` increments after each successful player half-step.
Time fields are reserved but are not advanced yet. `game_entry_reason` reports
`GAME_ENTRY_STARTUP`, `GAME_ENTRY_MOVEMENT`, `GAME_ENTRY_TRANSITION`, or
`GAME_ENTRY_LOAD` to room handlers without becoming persistent state. Room
code may use `flags` for small persistent facts. See `SAVE_GAME.md` for the
implemented sparse room-object journal and serialized save format.

### Game flags

`game_state.flags` contains 32 persistent bytes. Put story-specific flag
indices, masks, room IDs, and object IDs in `src/story.h`; keep reusable engine
contracts in `game.h` and `platform.h`. Prefer named indices instead of
unexplained numeric offsets:

```c
#define GAME_FLAG_LIGHTHOUSE_LIT  3u

if (game_state.flags[GAME_FLAG_LIGHTHOUSE_LIT] != 0u) {
    /* The flag is set. */
}
game_state.flags[GAME_FLAG_LIGHTHOUSE_LIT] = 1u; /* set */
game_state.flags[GAME_FLAG_LIGHTHOUSE_LIT] = 0u; /* clear */
```

A byte can hold several boolean facts when flag space becomes tight:

```c
#define GAME_FLAGS_HOUSE          4u
#define GAME_FLAG_DOOR_OPEN       0x01u

if (game_state.flags[GAME_FLAGS_HOUSE] & GAME_FLAG_DOOR_OPEN) {
    /* The door is open. */
}
game_state.flags[GAME_FLAGS_HOUSE] |= GAME_FLAG_DOOR_OPEN;
game_state.flags[GAME_FLAGS_HOUSE] &= (uint8_t)~GAME_FLAG_DOOR_OPEN;
```

Direct reads and writes are the current public API. These bytes are part of
`GameState`, so they survive room overlay replacement and are included in save
records.

## Room-callable API

The following functions are implemented by resident `src/game_support.c`.
Room overlays import them through the generated resolver; they are not copied
into every room binary:

```c
uint8_t game_inventory_count(uint8_t type);
uint8_t game_inventory_has(uint8_t type, uint8_t quantity);
uint8_t game_inventory_add(uint8_t type, uint8_t quantity);
uint8_t game_inventory_remove(uint8_t type, uint8_t quantity);

uint8_t game_health(void);
uint8_t game_mana(void);
void game_heal(uint8_t amount);
void game_damage(uint8_t amount);
uint8_t game_spend_mana(uint8_t amount);

void game_text_write(uint8_t line, const char* text, uint8_t color);
uint8_t game_room_script_entry(uint8_t key);
uint8_t game_transition_request(uint8_t room, uint8_t x, uint8_t y);
uint8_t game_take_object(uint8_t slot);
uint8_t game_take_tile(uint8_t tile_x, uint8_t tile_y);
void game_inventory_show(void);
```

Inventory type 0 is invalid. Adds saturate a matching slot at 255 and otherwise
use the first empty slot. Removes are atomic: insufficient total quantity
returns `PLATFORM_ERR_NOT_FOUND` without changing inventory.

`game_text_write()` uses the two-line bottom pager for a literal C string.
Text wraps at word boundaries, does not begin the second line with
whitespace, and pauses before continuing when output exceeds two lines. A
room's own text (and any accompanying logic - flag checks, giving an
object, branching) is not a literal string in room code at all; it's a
*room script* instead - see the next section.

`game_transition_request()` queues a transition because a room-code overlay
must not replace itself while one of its functions is executing. The resident
main loop processes the request on the next frame, calls `platform_room_enter()`,
loads the destination room-code overlay, synchronizes `game_state`, and calls
its `enter_room()` followed by `enter_tile()`. Coordinates are half-tile
coordinates and must be within `40x22`.

`game_take_object()` is a transactional persistent mutation. It accepts a
non-actor object slot, adds one item of that type to inventory, removes the map
object, and captures the room's sparse delta. Journal-capacity or inventory
failure restores both the object and inventory before returning an error.

`game_take_tile()` finds non-actor objects by their rendered intersection with
the selected tile rather than by hotspot alone. With multiple matches it runs
the resident object selector before delegating to `game_take_object()`.
`game_inventory_show()` loads the inventory/story overlay, blanks the VIC while
preparing a 40x25 text screen, and lists all 32 inventory slots in two columns.
Cursor keys move the selection marker, `U` dispatches the selected slot to
`story_use_inventory()`, and `I` closes the screen. The resident wrapper then
restores the charset split and redraws the current room.

All public platform APIs in `platform.h` are also callable when the generated
resolver finds a resident symbol. A missing import is a build error, not a
late runtime failure.

## Room scripts

A room's text and simple logic - Look/Use descriptions, room-entry and
tile-entry narration, anything that used to be a literal string in room
code - is authored as a *room script*: DSL source at
`assets/scripts/<hex room id>.script` (`tools/compile_script.py`, edited in
the asset editor's Script mode), compiled to the room-kind resource with
the same ID as the room itself (`PLATFORM_RESOURCE_KIND_ROOM` - an
independent 0x00-0xFF ID space from conversations and standalone scripts,
which live under `assets/scripts/conversations/` and
`assets/scripts/cutscenes/` respectively - see `PLATFORM_API.md`'s
"Generic cartridge resources").

A room script declares one or more numbered *entries*:

```
room 00 {
    entry STORY_ROOM00_LOOK_TREE {
        text "The tree has a knot hole."
    }
    entry STORY_ROOM00_USE_TREE {
        text "You knock on the tree, but nothing happens."
        give_object 9 1
    }
}
```

Room code runs one by key:

```c
uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 3u) {
        (void)game_room_script_entry(STORY_ROOM00_LOOK_TREE);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}
```

`game_room_script_entry()` returns 1 if a matching entry was found and run,
0 otherwise - a normal, expected outcome for a key nothing was authored for,
not an error. A room picks its own entry-key numbering (there's no global
convention); naming the keys via `#define` in `src/story.h` - the same file
flag indices already use, and `tools/compile_script.py`'s default symbol
source - keeps the DSL source and room code's call sites in sync through one
shared constant instead of a bare number on each side (`STORY_ROOM00_*`
above). A script's own opcode set (branching on flags, giving an object,
etc.) is documented in `tools/compile_script.py`'s module docstring.

The four exit-description bytes in the room file header (see
`PLATFORM_API.md`) are currently unread/reserved - not wired onto this
mechanism yet, so `platform_look_exit()` always shows a generic message
when the Look cursor is pushed off an edge.

## Room-code ABI and memory

Room code runs at `$9900-$9CFF`, a maximum of `$0400` bytes including BSS. The
first 24 file bytes are:

| Offset | Content |
|---:|---|
| 0 | `JMP enter_tile` |
| 3 | `JMP look_at` |
| 6 | `JMP enter_room` |
| 9 | `RC` magic |
| 11 | ABI version, currently 4 |
| 12 | room ID |
| 13 | loaded file size, little-endian |
| 15 | BSS offset from `$9900` |
| 17 | BSS size |
| 19 | 16-bit sum of bytes 24 through end |
| 21 | `JMP use_at` |

`tools/finalize_room_code.py` patches and validates this header. Resident calls
go through the four jump vectors, so adding private functions does not change
the ABI. ABI 4 added `use_at`; ABI 3 added `enter_room`; ABI 2 changed
`look_at` from one cardinal direction byte to the two tile-coordinate bytes
documented above. The loader rejects older room code.

A small assembly copier stages the file at `$A4E9` while 16 KiB EasyFlash ROM
is visible; it touches only hardware stack, zero page, and low DATA until the
cartridge is disabled. Activation copies the validated overlay to `$9900` and
zeros its BSS.

Room-code staging reuses rebuildable render/lighting work RAM. Therefore a
failed room-code prepare redraws the current room, and a successful room change
activates the overlay before the normal room draw rebuilds those buffers.

## Adding a room

1. Create/export `assets/XX` in Room mode.
2. Copy `rooms/template.c` to `rooms/XX.c` and implement the four handlers,
   or write `rooms/XX.rc` in the DSL above if the room's logic is one of
   the three mechanical shapes it covers.
3. Run `make d64 cartridge`.
4. Check `build/rooms/room-XX.map` if the `$0400` window overflows or an import
   cannot be resolved.

Do not keep pointers into an old room overlay or its BSS across a transition.
Store persistent values in `game_state` or mutate objects through the
save-aware APIs described in `SAVE_GAME.md`.
