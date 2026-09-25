/* Fallback for room 04's use_at. Handles:
 *   - the entry alcove's locked door (18,4): carrying the Brass Key
 *     (type 10) unlocks it - swaps the tile to plain floor (there's no
 *     separate "open door" graphic) so the player can pass. This is the
 *     *only* connection between the alcove and the rest of the cellar
 *     (assets/04's own tile grid blocks the old south passage at
 *     x=15,y=7-9), so the key is mandatory to reach the mirror, Mara, or
 *     the recipe stone at all - see MOONFEN_STORY_DESIGN.md section 5.
 *     Already unlocked or missing the key get their own flavor entry.
 *   - the moon mirror (12,1)/(12,2): carrying the Silver Shard (type 54)
 *     *and* wearing the Moon Brooch (STORY_STATE_MOON_BROOCH_WORN, toggled
 *     from the inventory screen - see story/story.c) repairs it - consumes
 *     the shard (the brooch is not consumed, just worn), sets
 *     STORY_STATE_MIRROR_REPAIRED, brightens the mirror's tiles
 *     (game_mirror_repair_apply(), src/game_support.c) and adds Mara
 *     (object type 37) to the room beside it (MOONFEN_STORY_DESIGN.md's
 *     "Mara then appears"). Already repaired, missing the shard, or
 *     carrying the shard without wearing the brooch each get their own
 *     flavor entry.
 *   - Mara (13,1), once present: STORY_ROOM04_TALK_MARA, a one-time
 *     reveal gated the same way as the other NPCs' first meeting (see
 *     assets/scripts/04.script), setting STORY_STATE_MARA_REVEALED and
 *     STORY_STATE_HOST_REVEALED.
 *   - falls through to game_ignite_brazier() for the two wall braziers,
 *     same as before this room had a door/mirror/Mara to handle. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 4u) {
        if (game_state.flags[14]) { /* STORY_STATE_BASEMENT_DOOR_OPEN */
            (void)game_room_script_entry(16u); /* STORY_ROOM04_DOOR_ALREADY_OPEN */
        } else if (game_inventory_has_one(10u)) { /* Brass Key */
            /* No game_world_capture_current() here - the world-state
             * delta cache only tracks room->objects[], never
             * room->tiles[] (confirmed in src/world.c), so this tile
             * swap needs its own reapply on every room entry instead;
             * see rooms/c/04_enter_room.c. */
            platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x] = 11u;
            game_state.flags[14] = 1u; /* STORY_STATE_BASEMENT_DOOR_OPEN */
            platform_room_draw(&platform_room, platform_player);
            (void)game_room_script_entry(15u); /* STORY_ROOM04_UNLOCK_DOOR */
        } else {
            (void)game_room_script_entry(17u); /* STORY_ROOM04_DOOR_LOCKED */
        }
        return GAME_USE_HANDLED;
    }

    if (tile_x == 12u && (tile_y == 1u || tile_y == 2u)) {
        if (game_state.flags[18]) { /* STORY_STATE_MIRROR_REPAIRED */
            (void)game_room_script_entry(7u); /* STORY_ROOM04_USE_MIRROR_ALREADY */
        } else if (!game_inventory_has_one(54u)) { /* Silver Shard */
            (void)game_room_script_entry(8u); /* STORY_ROOM04_USE_MIRROR_NO_ITEM */
        } else if (!game_state.flags[23]) { /* STORY_STATE_MOON_BROOCH_WORN */
            (void)game_room_script_entry(13u); /* STORY_ROOM04_USE_MIRROR_NEED_BROOCH */
        } else {
            uint8_t slot;
            game_inventory_remove_one(54u);
            game_state.flags[18] = 1u; /* STORY_STATE_MIRROR_REPAIRED */
            game_mirror_repair_apply();
            (void)platform_room_object_add(&platform_room, 37u /* Mara Moonfen */,
                                           26u, 2u, &slot);
            (void)game_world_capture_current();
            platform_room_draw(&platform_room, platform_player);
            (void)game_room_script_entry(6u); /* STORY_ROOM04_REPAIR_MIRROR */
        }
        return GAME_USE_HANDLED;
    }

    if (tile_x == 13u && tile_y == 1u && game_state.flags[18]) {
        /* STORY_STATE_MIRROR_REPAIRED - Mara only occupies this tile once
         * the mirror has been repaired (added above), so gate the same
         * way rather than risk dispatching to her with no object there. */
        (void)game_room_script_entry(10u); /* STORY_ROOM04_TALK_MARA */
        return GAME_USE_HANDLED;
    }

    return game_ignite_brazier(tile_x, tile_y);
}
