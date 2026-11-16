# Room-specific code API

Each compiled room has a native code/data bank:

```text
assets/00 + rooms/00.rc -> build/rooms/room00.c -> EasyFlash bank BANK_ROOM_FIRST + 0
```

The Makefile prepares the room record and compiles the DSL to C; Oscar64 links
that C with the rest of the game. `rooms/rooms.manifest` lists the rooms in
the build; `tools/generate_room_table.py` turns it into the resident dispatch
table (`build/rooms/room_table.h`) and the room-to-environment lookup
(`build/rooms/env_banks.h`).

## Where a room's behaviour lives

Almost everything a room does is authored in its *room script*,
`assets/scripts/<hex room id>.script` (see "Room scripts" below): text,
flag checks, giving and consuming inventory, swapping or adding room
objects, changing a tile, transitions, conversations. The `.rc` file only
maps the four engine hooks onto script entries. A hand-written C fragment
under `rooms/c/` is the escape hatch for the few things the script DSL
cannot read - the turn counter, the tile ID under the player or cursor -
and nothing story-specific lives in `src/` or `modules/`.

## Required handlers

The DSL generates room-prefixed versions of these handlers. C fragments use
the generic names below; the generator adds the room prefix and includes
`game.h`, `story.h` and the generated `object_types.h`. Externally callable
banked entries must remain `__noinline`:

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
entry narration, and re-applying tile changes on every visit.

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
the resident command print `Nothing happens.`. Generic inventory-item use
belongs in `story/story.c` instead (see `STORY_CODE_API.md`).

Before any of the four hooks runs, `src/room_dispatch.c` records the
*context tile* for the script interpreter's object statements: the selected
tile for `look_at`/`use_at`, the player's own tile for the entry hooks.

## Room-code DSL (.rc files)

```
enter_room: script STORY_ROOM04_ENTER_ROOM

enter_tile:
    at 16,1 -> script STORY_ROOM04_ASCEND_STAIRS
    default -> none

look_at:
    at 12,1 -> script STORY_ROOM04_LOOK_MIRROR
    default -> GAME_LOOK_DEFAULT

use_at:
    at 18,4 -> script STORY_ROOM04_USE_DOOR
    at 11,3 -> script STORY_ROOM04_USE_BRAZIER
    default -> GAME_USE_DEFAULT
```

`enter_room` and `enter_tile` take one line: `default` (empty handler),
`flag NAME increment`, `flag NAME set VALUE`, `script KEY` (run that room
script entry), or `asm "path"` (a C fragment, see below). `enter_tile` may
instead be an indented dispatch block of `at X,Y -> script KEY` lines
followed by `default -> none`, `default -> script KEY` or
`default -> asm "path"`; the player's tile is the key.

`look_at`/`use_at` each start an indented block of zero or more
`at X,Y -> script KEY` lines followed by a mandatory default line: a return
constant (`default -> GAME_LOOK_DEFAULT`), `default -> script KEY`, or
`default -> asm "path"`. Explicit coordinate matches run before the default.

`X`, `Y`, `KEY`, `VALUE` and return constants may be decimal/hex literals or
`#define` symbols from `src/story.h`, `src/game.h` and the generated
`build/object_types.h` (or any file passed via `--flags`). Lines starting
with `#` are comments.

The room's record, constants and handlers share one 16 KiB ROM bank; there
is no separate overlay limit.

### Escape hatch: C fragments

```text
enter_tile: asm "rooms/c/01_enter_tile.c"

look_at:
    default -> asm "rooms/c/01_look_at.c"
```

Entry fragments define `void enter_room(void)` or `void enter_tile(void)`.
Fallback fragments define `uint8_t look_at_fallback(uint8_t tile_x, uint8_t
tile_y)` or `use_at_fallback` with the same signature and return the usual
`GAME_LOOK_*`/`GAME_USE_*` result. The generator marks them `__noinline`,
substitutes the room-prefixed names and splices the C into the room's code
section. Use `story.h` and `OBJ_*` symbols rather than numbers; a fragment
that only checks inventory and flags should be a script entry instead. See
`rooms/c/00_enter_tile.c` (turn counter and road tile), `01_enter_tile.c`
(tile under the player) and `01_look_at.c` (cursor tile versus player tile).
The Makefile tracks `rooms/c/*.c` changes as generator dependencies.

## Room scripts

A room script declares numbered *entries*, run by key from the hooks above:

```
room 04 {
    entry STORY_ROOM04_USE_BRAZIER {
        check_object OBJ_BRAZIER {
            check_item OBJ_LIT_TAPER {
                replace_object OBJ_BRAZIER OBJ_LIT_BRAZIER
                text "You touch the taper's flame to the brazier, and it catches."
            } else {
                text "The brazier is cold. You have nothing to light it with."
            }
        } else {
            text "The brazier is already burning."
        }
    }
}
```

It compiles (`tools/compile_script.py`) to the room-kind resource with the
same ID as the room (`PLATFORM_RESOURCE_KIND_ROOM`), fetched by the banked
interpreter in `modules/script.c`. `game_room_script_entry(key)` returns 1
if a matching entry was found and run, 0 otherwise - a normal outcome for a
key nothing was authored for. Each room picks its own key numbering; name
the keys in `src/story.h` so the `.rc` and the `.script` share one constant.

Statements (operand encodings in `tools/compile_script.py`'s docstring; the
opcode numbers themselves are in `src/script_format.h`, which both the
compiler and the interpreter read):

| Statement | Effect |
|---|---|
| `text "..."` | Word-wrapped, paged text in the two bottom rows |
| `wait_key` | Wait for a fresh keypress |
| `portrait_show ID left/right`, `portrait_hide` | Speaker portrait |
| `set_flag F V`, `set_bit F B`, `clear_bit F B` | Story flags |
| `check_flag F op V { } [else { }]` | Branch on a flag (`== != < > <= >= &`) |
| `check_item TYPE [QTY] { } [else { }]` | Branch on carrying at least QTY of TYPE |
| `give_object TYPE QTY`, `take_item TYPE [QTY]` | Change the inventory |
| `check_object TYPE { } [else { }]` | Branch on an object of TYPE on the context tile |
| `replace_object FROM TO` | Retype objects of FROM on the context tile; journaled and redrawn |
| `add_object TYPE X Y` | Add an object at half-tile X,Y; journaled and redrawn |
| `set_tile X Y TILE` | Change one tile of the grid; redrawn, not journaled |
| `tile_color TILE COLOR` | Recolor a tile definition; not persisted |
| `room_transition ROOM X Y ["message"]`, `room_transition_here ROOM ["message"]` | Queue a transition |
| `lightning` | Two-frame white flash |
| `converse ID` | Start a conversation once this entry finishes |
| `sound ID` | Play a `SOUND_*` effect (`src/sounds.h`) on SID voice 3, blocking, then hand the voice back |
| `end_game` | After this entry's text is read, show the ending screen and restart into the title |

`TYPE` is an `OBJ_*` symbol from the generated `build/object_types.h`
(one per named type in `assets/objects.cobj`) or a number.

Two things are not journaled, because the world-state journal tracks room
objects only: a tile changed with `set_tile`, and a tile definition
recolored with `tile_color` (the shared tile table is also reloaded after
every save/load). Re-apply both from the room's `enter_room` entry, gated
on the flag that records the change - see `assets/scripts/04.script`'s
`STORY_ROOM04_ENTER_ROOM`.

A carried item's own light radius lights the player (`src/hero_light.c`),
so `give_object`/`take_item` of a light-emitting type changes the lighting
immediately; no script statement is needed for a lit torch.

An entry's `text` output waits for a fresh keypress before the engine clears
it and redraws the room; an entry that only runs effects adds no wait. A
script run from `enter_room`/`enter_tile` during a transition leaves the
display blanked until its first statement that needs it (text, a portrait,
a keypress wait), so a silent re-apply entry causes no flicker.

**Exit descriptions.** When the Look cursor is pushed off an edge whose
cardinal exit is enabled, the engine runs the room's reserved entry for
that direction - `GAME_SCRIPT_EXIT_NORTH`/`_EAST`/`_WEST`/`_SOUTH` (keys
252-255, `game.h`) - and falls back to `An exit.` if the room has none. A
room's own keys stay below 252. The four exit-description bytes in the
room file header remain unused.

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
`GAME_ENTRY_LOAD` to room handlers without becoming persistent state. See
`SAVE_GAME.md` for the sparse room-object journal and serialized save format.

### Game flags

`game_state.flags` contains 32 persistent bytes. Story-specific flag indices
and entry keys live in `src/story.h`; object-type IDs come from the generated
`build/object_types.h`; reusable engine contracts stay in `game.h` and
`platform.h`. Scripts and fragments name flags, never bare indices. A byte
can hold several boolean facts (`set_bit`/`clear_bit`/`check_flag F & BIT`,
or `|=`/`&=` in C) when flag space becomes tight.

## Room-callable API

C fragments can call the resident engine API in `src/game_support.c`:

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
```

Inventory type 0 is invalid. Adds saturate a matching slot at 255 and otherwise
use the first empty slot. Removes are atomic: insufficient total quantity
returns `PLATFORM_ERR_NOT_FOUND` without changing inventory. Both re-derive
the player's light from the inventory.

`game_transition_request()` defers the transition until the next main-loop
frame, so current handlers can return before the active room/environment
changes. The engine loads the destination record, selects its handler bank,
synchronizes `game_state`, and calls `enter_room()` then `enter_tile()`. A
transition that fails (blocked arrival tile, full journal, no free actor
slot) leaves the old room active and tells the player so. Coordinates are
half-tile coordinates within `40x22`.

`game_take_object()` is a transactional persistent mutation. `game_take_tile()`
finds non-actor objects by their rendered intersection with the selected tile
and runs the object selector when several match.

Banked handlers can call resident APIs and functions within their selected
bank. Cross-bank services need the established resident wrappers/gates;
ordinary pointers and declarations do not select a bank. See
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md).

## Room-code ABI and memory

`src/room_dispatch.c` looks up a room ID in the generated table and calls its
native handlers through `far_call_bank`. There is no runtime room-code header,
checksum, relocation copy or per-entry BSS clear. Mutable state remains in
resident RAM; use `game_state` flags/deltas for state that must persist in saves.

The generated room record must remain at offset zero in its bank because the
data fetch uses that offset. Keep its data section first and verify the map
when adding constants. Do not retain bank-local pointers for later use with a
different bank selected.

## Adding a room

1. Export `assets/XX` from the asset editor and write `rooms/XX.rc`.
2. Write `assets/scripts/XX.script` and add its entry keys (and any new
   flags) to `src/story.h`.
3. Add one line to `rooms/rooms.manifest`: the hex ID and its environment
   module (`rain`, `tavern`, `tavern_muffled`, `cellar`, or `none`).
4. Build. Room `XX` occupies EasyFlash bank `BANK_ROOM_FIRST + XX`; the
   compiler rejects a collision, and `src/banks.h` documents the range.
5. Inspect `build/game.map` for the room's data/code and any new resident
   state, then test entry, exit, input, narration and the environment in VICE.
