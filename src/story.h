#ifndef STORY_H
#define STORY_H

#include <stdint.h>

/* Story-owned GameState.flags byte indices. Keep assigned values stable. */
#define STORY_STATE_ROOM_00_TILE_ENTRY_COUNT  0u
#define STORY_STATE_ROOM_01_TILE_ENTRY_COUNT  1u

/* Room-script entry keys (game_room_script_entry() in game.h; see
 * assets/scripts/<room id>.script). Each room picks its own numbering - no
 * global convention - but assigning names here keeps a room's C call sites
 * and its .script source (tools/compile_script.py resolves symbols against
 * this file by default) using the same constant instead of a bare number.
 * Keep assigned values stable per room, same as the flag indices above. */
#define STORY_ROOM00_LOOK_TREE  1u
#define STORY_ROOM00_USE_TREE   2u

/*
 * Story overlay hook for U on the selected inventory entry. The argument is
 * the stable GameState.inventory slot, so both type and quantity are
 * available. Return GAME_USE_HANDLED after producing the complete result.
 */
uint8_t __fastcall__ story_use_inventory(uint8_t inventory_slot);

#endif
