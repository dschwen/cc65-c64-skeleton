#ifndef STORY_H
#define STORY_H

#include <stdint.h>

/* Story-owned GameState.flags byte indices. Keep assigned values stable. */
#define STORY_STATE_ROOM_00_TILE_ENTRY_COUNT  0u
#define STORY_STATE_ROOM_01_TILE_ENTRY_COUNT  1u

/*
 * Story overlay hook for U on the selected inventory entry. The argument is
 * the stable GameState.inventory slot, so both type and quantity are
 * available. Return GAME_USE_HANDLED after producing the complete result.
 */
uint8_t __fastcall__ story_use_inventory(uint8_t inventory_slot);

#endif
