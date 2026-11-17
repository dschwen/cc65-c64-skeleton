#ifndef STORY_H
#define STORY_H

#include <stdint.h>

/*
 * Story-owned constants for The Last Guest at Moonfen Inn: GameState.flags
 * byte indices, room-script entry keys, and the few tile IDs story logic
 * reads. Object-type IDs are NOT listed here - they come from the
 * generated build/object_types.h (OBJ_<NAME>, one per named type in
 * assets/objects.cobj), which both the script compiler and generated room
 * code see. Keep assigned values stable: flags are saved to disk, and
 * entry keys are what a room's .script and its .rc agree on.
 */

/* ---- GameState.flags byte indices ------------------------------------ */
#define STORY_STATE_ROOM_00_TILE_ENTRY_COUNT  0u
#define STORY_STATE_ROOM_01_TILE_ENTRY_COUNT  1u
#define STORY_STATE_ROOM_00_MILESTONE_SEEN    2u
#define STORY_STATE_ROOM_00_MYSTERY_TRIGGERED 3u
#define STORY_STATE_ROOM_01_ARRIVAL_SHOWN     4u
/* A packed byte: bit 1 = Blue Seal found (room 01); see set_bit/check_flag
 * in assets/scripts/01.script. */
#define QUEST_FLAGS                           5u
#define QUEST_BIT_SEAL_FOUND                  1u
#define STORY_STATE_ROOM_02_MET_ORRA           6u
#define STORY_STATE_ROOM_02_MET_VEY             7u
#define STORY_STATE_ROOM_02_MET_CALDUS          8u
#define STORY_STATE_ROOM_02_CALDUS_FREED        9u
#define STORY_STATE_ROOM_05_MET_PELL           10u
#define STORY_STATE_ROOM_05_PELL_INVITE_SEEN   11u
#define STORY_STATE_ROOM_05_BLUE_DOOR_SEEN     12u
#define STORY_STATE_ROOM_05_PELL_HELPED        13u
#define STORY_STATE_BASEMENT_DOOR_OPEN         14u
#define STORY_STATE_ROOM_05_SILVER_DOOR_SEEN   15u
#define STORY_STATE_ROOM_02_ORRA_HELPED        16u
#define STORY_STATE_ROOM_05_ROSE_DOOR_SEEN     17u
#define STORY_STATE_MIRROR_REPAIRED            18u
#define STORY_STATE_MARA_REVEALED              19u
#define STORY_STATE_BROTH_SERVED               20u
#define STORY_STATE_HOST_REVEALED              21u
#define STORY_STATE_FINALE_DONE                22u
#define STORY_STATE_MOON_BROOCH_WORN           23u
#define STORY_STATE_ROOM_05_INVITATION_TAKEN   24u

/* ---- Tile IDs story logic reads (assets/tiles.ctil) ------------------- */
#define STORY_TILE_WATER         2u   /* room 01's puddle, hiding the Blue Seal */
#define STORY_TILE_DOOR_OPEN     5u   /* walkable open-door tile, rooms 03/04/05 */
#define STORY_TILE_DOOR_LOCKED   6u   /* room 04's alcove door before the Brass Key */
#define STORY_TILE_ROAD_A        22u  /* room 00's road, where the inn reveals itself */
#define STORY_TILE_ROAD_B        23u
#define STORY_TILE_STORM_WINDOW  65u
#define STORY_TILE_MIRROR_FRAME  83u  /* the moon mirror's two tiles, room 04 only */
#define STORY_TILE_MIRROR_GLASS  84u
#define STORY_COLOR_MIRROR_LIT   14u  /* light blue: the repaired mirror's glow */

/* The inn reveals itself (room 00's enter_tile) only after this many turns. */
#define STORY_ROOM00_MYSTERY_MIN_TURNS 20u

/* ---- Room-script entry keys (game_room_script_entry(), assets/scripts/
 * <room id>.script). Each room picks its own numbering. ---------------- */
#define STORY_ROOM00_LOOK_TREE          1u
#define STORY_ROOM00_USE_TREE           2u
#define STORY_ROOM00_LOOK_MILESTONE     3u
#define STORY_ROOM00_MYSTERY_SEQUENCE   4u

#define STORY_ROOM01_ARRIVAL            1u
#define STORY_ROOM01_ENTER_INN          2u
#define STORY_ROOM01_FIND_SEAL          3u
#define STORY_ROOM01_SEARCH_FAR         4u
#define STORY_ROOM01_USE_BARREL         5u
#define STORY_ROOM01_LOOK_BARREL        8u

#define STORY_ROOM02_LEAVE_INN          1u
#define STORY_ROOM02_TALK_ORRA          2u
#define STORY_ROOM02_LOOK_ORRA          3u
#define STORY_ROOM02_TALK_VEY           4u
#define STORY_ROOM02_LOOK_VEY           5u
#define STORY_ROOM02_TALK_CALDUS        6u
#define STORY_ROOM02_LOOK_CALDUS        7u
#define STORY_ROOM02_ENTER_KITCHEN      8u
#define STORY_ROOM02_USE_HEARTH         10u
#define STORY_ROOM02_ENTER_CORRIDOR     12u
#define STORY_ROOM02_USE_TABLE          16u
#define STORY_ROOM02_RING_BELL          19u
#define STORY_ROOM02_LOOK_BELL          20u
#define STORY_ROOM02_LOOK_TABLE         21u

#define STORY_ROOM03_LEAVE_KITCHEN      1u
#define STORY_ROOM03_LOOK_SHELVES       3u
#define STORY_ROOM03_LOOK_OIL           4u
#define STORY_ROOM03_LOOK_TAPER         5u
#define STORY_ROOM03_LOOK_CAULDRON      6u
#define STORY_ROOM03_DESCEND_STAIRS     7u
#define STORY_ROOM03_LOOK_STAIRS_DOWN   8u
#define STORY_ROOM03_LOOK_WINDOW        9u
#define STORY_ROOM03_LOOK_KETTLE        12u
#define STORY_ROOM03_USE_KETTLE         13u
#define STORY_ROOM03_USE_CAULDRON       16u

#define STORY_ROOM04_ASCEND_STAIRS      1u
#define STORY_ROOM04_LOOK_STAIRS_UP     2u
#define STORY_ROOM04_USE_BRAZIER        3u
#define STORY_ROOM04_LOOK_MIRROR        5u
#define STORY_ROOM04_USE_MIRROR         6u
#define STORY_ROOM04_LOOK_MARA          9u
#define STORY_ROOM04_TALK_MARA          10u
#define STORY_ROOM04_LOOK_RECIPE_STONE  11u
#define STORY_ROOM04_LOOK_WITCHLIGHT    12u
#define STORY_ROOM04_LOOK_DOOR          14u
#define STORY_ROOM04_USE_DOOR           15u
#define STORY_ROOM04_ENTER_ROOM         18u

#define STORY_ROOM05_LEAVE_CORRIDOR     1u
#define STORY_ROOM05_LOOK_WINDOW        2u
#define STORY_ROOM05_USE_GUEST_DOOR     4u
#define STORY_ROOM05_LOOK_PELL          5u
#define STORY_ROOM05_TALK_PELL          6u
#define STORY_ROOM05_LOOK_INVITATION    7u
#define STORY_ROOM05_LOOK_BLUE_DOOR     8u
#define STORY_ROOM05_LOOK_SILVER_DOOR   11u
#define STORY_ROOM05_LOOK_ROSE_DOOR     12u

/*
 * Story hook for U on the selected inventory entry (modules/inventory.c).
 * The argument is the stable GameState.inventory slot, so both type and
 * quantity are available. Return GAME_USE_HANDLED after producing the
 * complete result.
 */
uint8_t story_use_inventory(uint8_t inventory_slot);

#endif
