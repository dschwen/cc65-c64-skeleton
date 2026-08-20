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
void enter_tile(void);
uint8_t __fastcall__ look_at(uint8_t direction);
```

`enter_tile()` takes no coordinates. Read `game_state.player_x` and
`game_state.player_y`; both are half-tile/character coordinates. The engine
calls it after initial startup, after entering a room, and whenever successful
movement changes the player's hotspot tile. A one-character half-step within
the same 2x2 tile does not call it.

`look_at()` receives one of `PLATFORM_DIRECTION_NORTH`, `_EAST`, `_WEST`, or
`_SOUTH`. Return `GAME_LOOK_HANDLED` after producing room-specific output, or
`GAME_LOOK_DEFAULT` to let `platform_look_direction()` list objects or the
exit description.

Example:

```c
#include "game.h"

void enter_tile(void) {
    if ((game_state.player_x >> 1) == 4u &&
        (game_state.player_y >> 1) == 2u) {
        game_state.flags[3] = 1u;
    }
}

uint8_t __fastcall__ look_at(uint8_t direction) {
    if (direction == PLATFORM_DIRECTION_NORTH &&
        (game_state.player_x >> 1) == 4u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "The sea is rough today.", 1u);
        platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}
```

## Global game state

`game_state` is resident at `$84E9` and survives overlay replacement:

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
`GAME_ENTRY_LOAD` to `enter_tile()` without becoming persistent state. Room
code may use `flags` for small persistent facts. See `SAVE_GAME.md` for the
implemented sparse room-object journal and serialized save format.

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
void game_text_write_room(uint8_t line, uint8_t text_offset, uint8_t color);
void game_dialog_show(const char* line0, const char* line1,
                      const char* line2, uint8_t color);
void game_dialog_show_room(uint8_t line0, uint8_t line1,
                           uint8_t line2, uint8_t color);
uint8_t game_transition_request(uint8_t room, uint8_t x, uint8_t y);
uint8_t game_take_object(uint8_t slot);
uint8_t game_take_direction(uint8_t direction);
void game_inventory_show(void);
```

Inventory type 0 is invalid. Adds saturate a matching slot at 255 and otherwise
use the first empty slot. Removes are atomic: insufficient total quantity
returns `PLATFORM_ERR_NOT_FOUND` without changing inventory.

`game_text_write*()` replaces one of the two bottom lines. The `_room` variant
reads an ASCII string from the current room's 256-byte text pool.
`game_dialog_show*()` opens the eight-sprite, three-line overlay at the standard
position; callers hide it with `platform_overlay_hide()` when appropriate.

`game_transition_request()` queues a transition because an overlay must not
replace itself while one of its functions is executing. The resident main loop
processes the request on the next frame, calls `platform_room_enter()`, loads
the destination overlay, synchronizes `game_state`, and calls its
`enter_tile()`. Coordinates are half-tile coordinates and must be within
`40x22`.

`game_take_object()` is a transactional persistent mutation. It accepts a
non-actor object slot, adds one item of that type to inventory, removes the map
object, and captures the room's sparse delta. Journal-capacity or inventory
failure restores both the object and inventory before returning an error.

`game_take_direction()` examines the adjacent map tile in a cardinal
direction, skips actors, and takes the first eligible object by room slot.
`game_inventory_show()` blanks the VIC while preparing a 40x25 text screen,
lists all 32 inventory slots in two columns, waits for a fresh keypress, then
restores the charset split and redraws the current room.

All public platform APIs in `platform.h` are also callable when the generated
resolver finds a resident symbol. A missing import is a build error, not a
late runtime failure.

## Exit descriptions

Room format 3 stores four byte offsets into `platform_room.text`, one per
cardinal exit. Use the Room editor's description selectors, or query them:

```c
const char* description =
    platform_room_exit_description(&platform_room, direction);
```

The function returns `NULL` for an absent description. Generic look output
uses the description only when the adjacent location lies outside the map and
the corresponding exit is enabled. It falls back to `an exit.`.

## Overlay ABI and memory

Room code runs at `$9900-$9CFF`, a maximum of `$0400` bytes including BSS. The
first 20 file bytes are:

| Offset | Content |
|---:|---|
| 0 | `JMP enter_tile` |
| 3 | `JMP look_at` |
| 6 | `RC` magic |
| 8 | ABI version, currently 1 |
| 9 | room ID |
| 10 | loaded file size, little-endian |
| 12 | BSS offset from `$9900` |
| 14 | BSS size |
| 16 | 16-bit sum of bytes 20 through end |
| 18 | reserved |

`tools/finalize_room_code.py` patches and validates this header. Resident calls
go through the two jump vectors, so adding private functions does not change
the ABI.

Disk loading uses KERNAL I/O with BASIC hidden (`$01 = $36`), staging the file
at `$A4E9`. EasyFlash uses a small assembly copier while 16 KiB ROM is visible;
it touches only hardware stack, zero page, and low DATA until the cartridge is
disabled. Activation copies the validated overlay to `$9900` and zeros its BSS.

Room-code staging reuses rebuildable render/lighting work RAM. Therefore a
failed room-code prepare redraws the current room, and a successful room change
activates the overlay before the normal room draw rebuilds those buffers.

## Adding a room

1. Create/export `assets/XX` in Room mode.
2. Add `rooms/XX.c` with both required handlers.
3. Run `make d64 cartridge`.
4. Check `build/rooms/room-XX.map` if the `$0400` window overflows or an import
   cannot be resolved.

Do not keep pointers into an old room overlay or its BSS across a transition.
Store persistent values in `game_state` or mutate objects through the
save-aware APIs described in `SAVE_GAME.md`.
