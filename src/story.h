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
#define STORY_STATE_ROOM_05_MET_PELL           10u
#define STORY_STATE_ROOM_05_PELL_INVITE_SEEN   11u
#define STORY_STATE_ROOM_05_BLUE_DOOR_SEEN     12u
#define STORY_STATE_ROOM_05_PELL_HELPED        13u
#define STORY_STATE_CELLAR_OPEN                14u
#define STORY_STATE_ROOM_05_SILVER_DOOR_SEEN   15u
#define STORY_STATE_ROOM_02_ORRA_HELPED        16u
#define STORY_STATE_ROOM_05_ROSE_DOOR_SEEN     17u
#define STORY_STATE_MIRROR_REPAIRED            18u
#define STORY_STATE_MARA_REVEALED              19u
#define STORY_STATE_BROTH_SERVED               20u
#define STORY_STATE_HOST_REVEALED              21u
#define STORY_STATE_FINALE_DONE                22u

/* Object type IDs 54+ reserved for the rest of MOONFEN_STORY_DESIGN.md
 * (assets/objects.cobj; types 1-53 already assigned, see that file's own
 * records for the roster) - listed here up front so later steps don't
 * collide, even though each is only authored/placed when its own step
 * lands:
 *   54 Silver Shard   55 Moon Brooch     56 Blank Letter
 *   57 Orra Invite    58 Empty Cup       59 Rainwater
 *   60 Hearth Ash     61 Guest's Broth
 * Moonmint reuses the already-placed "Moonshroom" (type 42, room 01) under
 * its design-doc name rather than adding a new type - see that room's own
 * object table. */

/* QUEST_FLAGS bit assignments (single shared byte, see assets/scripts/*.script
 * set_bit/check_flag usage): bit 1 = Blue Seal found (room 01). Whether the
 * Long Taper has been lit is no longer tracked as a separate bit - lighting
 * it swaps the inventory item itself from Long Taper (type 52) to Lit
 * Taper (type 53, see rooms/asm/02_use_at.s), so the item's own type is
 * the persistent state. */

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
#define STORY_ROOM02_LIGHT_TAPER_ALREADY 13u

#define STORY_ROOM03_LEAVE_KITCHEN 1u
#define STORY_ROOM03_LOOK_SHELVES  3u
#define STORY_ROOM03_LOOK_OIL      4u
#define STORY_ROOM03_LOOK_TAPER    5u
#define STORY_ROOM03_LOOK_CAULDRON 6u
#define STORY_ROOM03_DESCEND_STAIRS 7u
#define STORY_ROOM03_LOOK_STAIRS_DOWN 8u
#define STORY_ROOM03_LOOK_WINDOW 9u
#define STORY_ROOM03_WARD_OPENED  10u
#define STORY_ROOM03_WARD_BLOCKED 11u

#define STORY_ROOM04_ASCEND_STAIRS   1u
#define STORY_ROOM04_LOOK_STAIRS_UP  2u
#define STORY_ROOM04_LIGHT_BRAZIER   3u
#define STORY_ROOM04_LIGHT_BRAZIER_NO_ITEM 4u

#define STORY_ROOM05_LEAVE_CORRIDOR  1u
#define STORY_ROOM05_LOOK_WINDOW     2u
#define STORY_ROOM05_LOOK_GUEST_DOOR 3u
#define STORY_ROOM05_USE_GUEST_DOOR  4u
#define STORY_ROOM05_LOOK_PELL       5u
#define STORY_ROOM05_TALK_PELL       6u
#define STORY_ROOM05_LOOK_INVITATION 7u
#define STORY_ROOM05_LOOK_BLUE_DOOR  8u
#define STORY_ROOM05_PELL_EARLY_SEAL 9u
#define STORY_ROOM05_PELL_RESOLVE    10u
#define STORY_ROOM05_LOOK_SILVER_DOOR 11u

/*
 * Story overlay hook for U on the selected inventory entry. The argument is
 * the stable GameState.inventory slot, so both type and quantity are
 * available. Return GAME_USE_HANDLED after producing the complete result.
 */
uint8_t story_use_inventory(uint8_t inventory_slot);

#endif
