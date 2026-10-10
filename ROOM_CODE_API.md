# Room-specific code API

Each compiled room has a native code/data bank:

```text
assets/00 + rooms/00.rc -> build/rooms/room00.c -> EasyFlash bank 8
```

The Makefile prepares the room record and compiles the DSL to C; Oscar64 links
that C with the rest of the game. `rooms/room_table.h` supplies the resident
dispatch table. The current build includes rooms 00-05.

## Required handlers

The DSL generates room-prefixed versions of these handlers. C fragments use
the generic names below; the generator adds the room prefix and includes
`game.h`. Externally callable banked entries must remain `__noinline`:

```c
__noinline void enter_room(void);
__noinline void enter_tile(void);
__noinline uint8_t look_at(uint8_t tile_x, uint8_t tile_y);
__noinline uint8_t use_at(uint8_t tile_x, uint8_t tile_y);
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

__noinline void enter_room(void) {
    if (game_entry_reason == GAME_ENTRY_STARTUP) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You wake by the sea.", 1u);
    }
}

__noinline void enter_tile(void) {
    if ((game_state.player_x >> 1) == 4u &&
        (game_state.player_y >> 1) == 2u) {
        game_state.flags[3] = 1u;
    }
}

__noinline uint8_t look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 4u && tile_y == 2u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "The sea is rough today.", 1u);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}

__noinline uint8_t use_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 7u && tile_y == 3u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "The door opens.", 1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
```

## Room-code DSL (.rc files)

The room DSL in `rooms/XX.rc` covers empty handlers, flag changes and
coordinate dispatch to script entries. `tools/compile_room.py` emits
`build/rooms/roomXX.c`, including the prepared room asset with `#embed`.
The current Makefile requires the DSL source; it does not select a standalone
`rooms/XX.c` alternative.

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
more `at X,Y -> script KEY` lines followed by a mandatory default line -
either `default -> RETURN_CONST` or `default -> asm "path"` (same escape
hatch, see below). `X`, `Y`, `KEY`, and `RETURN_CONST` may be
decimal/hex literals or `#define` symbols from `src/story.h`/`src/game.h`
(or files passed via `--flags`).

The DSL keeps repetitive dispatch logic concise. The room's record, constants
and handlers share one 16 KiB ROM bank; there is no separate 1 KiB overlay limit.

### Escape hatch: genuinely custom logic

The parser retains the keyword `asm`, but the referenced file is now a C
fragment, not assembler:

```text
enter_tile: asm "rooms/c/01_enter_tile.c"

look_at:
    default -> asm "rooms/c/01_look_at.c"
```

Entry fragments define `__noinline void enter_room(void)` or
`__noinline void enter_tile(void)`. Fallback fragments define
`__noinline uint8_t look_at_fallback(uint8_t tile_x, uint8_t tile_y)`
or `use_at_fallback` with the same signature. Return the usual
`GAME_LOOK_*`/`GAME_USE_*` result.

The generator substitutes the room-prefixed names and splices the C into the
room's code section. Explicit coordinate matches run before fallback code.
Use normal C parameters/results, not a hand-coded stack ABI. See
`rooms/c/01_enter_tile.c`, `01_look_at.c` and `01_use_at.c`.
The Makefile tracks `rooms/c/*.c` changes as generator dependencies.

## Global game state

`game_state` is compiler-allocated in resident main RAM and survives bank changes:

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
`GameState`, so they survive room transitions and are included in save
records.

## Room-callable API

The following functions are implemented by resident `src/game_support.c`.
Room handlers call their declared interfaces in the whole-program build; they are not copied
into every room binary:

```c
uint8_t game_inventory_count(uint8_t type);
uint8_t game_inventory_has(uint8_t type, uint8_t quantity);
uint8_t game_inventory_add(uint8_t type, uint8_t quantity);
uint8_t game_inventory_remove(uint8_t type, uint8_t quantity);

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

`game_transition_request()` defers the transition until the next main-loop
frame, so current handlers can return before the active room/environment
changes. The engine loads the destination record, selects its handler bank,
synchronizes `game_state`, and calls `enter_room()` then `enter_tile()`.
Coordinates are half-tile coordinates within `40x22`.

`game_take_object()` is a transactional persistent mutation. It accepts a
non-actor object slot, adds one item of that type to inventory, removes the map
object, and captures the room's sparse delta. Journal-capacity or inventory
failure restores both the object and inventory before returning an error.

`game_take_tile()` finds non-actor objects by their rendered intersection with
the selected tile rather than by hotspot alone. With multiple matches it runs
the resident object selector before delegating to `game_take_object()`.
The main loop reaches `game_inventory_show()` in bank 5 through `far_call_bank`. It
prepares a 40x25 text screen and lists all 32 inventory slots in two columns.
Cursor keys move the selection marker, `U` dispatches the selected slot to
`story_use_inventory()`, and `I` closes the screen. The service cleanup then
restores the charset split and redraws the current room.

Banked handlers can call resident APIs and functions within their selected
bank. Cross-bank services need the established resident wrappers/gates;
ordinary pointers and declarations do not select a bank. See
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md).

## Room scripts

A room's text and simple logic - Look/Use descriptions, room-entry and
tile-entry narration, anything that used to be a literal string in room
code - is authored as a *room script*: DSL source at
`assets/scripts/<hex room id>.script` (`tools/compile_script.py`, edited in
the asset editor's Script mode), compiled to the room-kind resource with
the same ID as the room itself (`PLATFORM_RESOURCE_KIND_ROOM` - an
ID space reserved separately from conversations and standalone scripts).
Only room scripts are currently compiled/embedded and supported by the resource
fetch path; the other kinds are not yet wired up. See `PLATFORM_API.md`'s
"Generic cartridge resources".

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
__noinline uint8_t look_at(uint8_t tile_x, uint8_t tile_y) {
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

An entry's `text` output waits for a fresh keypress before the engine clears
it and redraws the room, the same way the bottom pager already waits between
pages of a message longer than two lines - so a room's own description text
is always given a chance to be read, however long it is, with no `wait_key`
needed just to keep it on screen. An entry
that only runs effects or a transition, with no `text` at all, does not add
this wait.

The four exit-description bytes in the room file header (see
`PLATFORM_API.md`) are currently unread/reserved - not wired onto this
mechanism yet, so `platform_look_exit()` always shows a generic message
when the Look cursor is pushed off an edge.

## Room-code ABI and memory

`src/room_dispatch.c` looks up a room ID in `rooms/room_table.h` and calls its
native handlers through `far_call_bank`. There is no runtime room-code header,
checksum, relocation copy or per-entry BSS clear. Mutable state remains in
resident RAM; use `game_state` flags/deltas for state that must persist in saves.

The generated room record must remain at offset zero in its bank because the
data fetch uses that offset. Keep its data section first and verify the map
when adding constants. Do not retain bank-local pointers for later use with a
different bank selected.

## Adding a room

1. Export `assets/XX`, write `rooms/XX.rc`, and add any C fragments under `rooms/c/`.
2. Add `assets/scripts/XX.script` (even an empty room script if appropriate)
   and its symbols in `src/story.h`.
3. Add the ID to Makefile `ROOM_IDS` and declarations/table entries in
   `rooms/room_table.h`. Generated symbols use lowercase hexadecimal IDs.
4. Check the generator's `BANK_ROOM_FIRST + room_id` assignment against
   `src/banks.h`. The next sequential room currently collides with bank 14;
   extending the list requires an explicit allocation change, not just a new file.
5. Update `rooms/env_banks.h` if the room needs an environment.
6. Build and inspect `build/game.map` for room data/code and any new resident
   state, then test entry, exit, input, narration and the environment in VICE.

Use save-aware mutation APIs from [SAVE_GAME.md](SAVE_GAME.md); ordinary
resident globals survive room changes but are not automatically serialized.
