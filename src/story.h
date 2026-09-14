#ifndef STORY_H
#define STORY_H

#include <stdint.h>

/* Story-owned GameState.flags byte indices. Keep assigned values stable. */
#define STORY_STATE_ROOM_00_TILE_ENTRY_COUNT  0u
#define STORY_STATE_ROOM_01_TILE_ENTRY_COUNT  1u
#define STORY_STATE_ROOM_00_MILESTONE_SEEN    2u
#define STORY_STATE_ROOM_00_MYSTERY_TRIGGERED 3u
#define STORY_STATE_ROOM_01_ARRIVAL_SHOWN     4u
#define QUEST_FLAGS                           5u
#define STORY_STATE_ROOM_02_MET_ORRA           6u
#define STORY_STATE_ROOM_02_MET_VEY             7u
#define STORY_STATE_ROOM_02_MET_CALDUS          8u
#define STORY_STATE_ROOM_02_CALDUS_FREED        9u

/* QUEST_FLAGS bit assignments (single shared byte, see assets/scripts/*.script
 * set_bit/check_flag usage): bit 1 = Blue Seal found (room 01), bit 2 = the
 * Long Taper has been lit at the tavern hearth (room 02). */
#define QUEST_FLAG_TAPER_LIT 2u

/* Room-script entry keys (game_room_script_entry() in game.h; see
 * assets/scripts/<room id>.script). Each room picks its own numbering - no
 * global convention - but assigning names here keeps a room's C call sites
 * and its .script source (tools/compile_script.py resolves symbols against
 * this file by default) using the same constant instead of a bare number.
 * Keep assigned values stable per room, same as the flag indices above. */
#define STORY_ROOM00_LOOK_TREE  1u
#define STORY_ROOM00_USE_TREE   2u
#define STORY_ROOM00_LOOK_MILESTONE     3u
#define STORY_ROOM00_MYSTERY_SEQUENCE   4u

#define STORY_ROOM01_ARRIVAL     1u
#define STORY_ROOM01_ENTER_INN   2u
#define STORY_ROOM01_FIND_SEAL   3u
#define STORY_ROOM01_SEARCH_FAR  4u

#define STORY_ROOM02_LEAVE_INN   1u
#define STORY_ROOM02_TALK_ORRA   2u
#define STORY_ROOM02_LOOK_ORRA   3u
#define STORY_ROOM02_TALK_VEY    4u
#define STORY_ROOM02_LOOK_VEY    5u
#define STORY_ROOM02_TALK_CALDUS 6u
#define STORY_ROOM02_LOOK_CALDUS 7u
#define STORY_ROOM02_ENTER_KITCHEN 8u
#define STORY_ROOM02_CALDUS_FREE   9u
#define STORY_ROOM02_LIGHT_TAPER      10u
#define STORY_ROOM02_LIGHT_TAPER_NO_ITEM 11u
#define STORY_ROOM02_ENTER_CORRIDOR   12u

#define STORY_ROOM03_LEAVE_KITCHEN 1u
#define STORY_ROOM03_LOOK_SHELVES  3u
#define STORY_ROOM03_LOOK_OIL      4u
#define STORY_ROOM03_LOOK_TAPER    5u
#define STORY_ROOM03_LOOK_CAULDRON 6u
#define STORY_ROOM03_DESCEND_STAIRS 7u
#define STORY_ROOM03_LOOK_STAIRS_DOWN 8u
#define STORY_ROOM03_LOOK_WINDOW 9u

#define STORY_ROOM04_ASCEND_STAIRS   1u
#define STORY_ROOM04_LOOK_STAIRS_UP  2u

#define STORY_ROOM05_LEAVE_CORRIDOR  1u
#define STORY_ROOM05_LOOK_WINDOW     2u
#define STORY_ROOM05_LOOK_GUEST_DOOR 3u
#define STORY_ROOM05_USE_GUEST_DOOR  4u

/*
 * Story overlay hook for U on the selected inventory entry. The argument is
 * the stable GameState.inventory slot, so both type and quantity are
 * available. Return GAME_USE_HANDLED after producing the complete result.
 */
uint8_t __fastcall__ story_use_inventory(uint8_t inventory_slot);

#endif
