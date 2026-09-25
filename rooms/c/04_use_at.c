/* Fallback for room 04's use_at. Handles:
 *   - the moon mirror (12,1)/(12,2): carrying the Silver Shard (type 54)
 *     repairs it - consumes the shard, sets STORY_STATE_MIRROR_REPAIRED,
 *     brightens the mirror's tiles (game_mirror_repair_apply(),
 *     src/game_support.c) and adds Mara (object type 37) to the room
 *     beside it (MOONFEN_STORY_DESIGN.md's "Mara then appears").
 *     Already repaired or missing the shard get their own flavor entry.
 *   - Mara (13,1), once present: STORY_ROOM04_TALK_MARA, a one-time
 *     reveal gated the same way as the other NPCs' first meeting (see
 *     assets/scripts/04.script), setting STORY_STATE_MARA_REVEALED and
 *     STORY_STATE_HOST_REVEALED.
 *   - falls through to game_ignite_brazier() for the two wall braziers,
 *     same as before this room had a mirror/Mara to handle. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 12u && (tile_y == 1u || tile_y == 2u)) {
        if (game_state.flags[18]) { /* STORY_STATE_MIRROR_REPAIRED */
            (void)game_room_script_entry(7u); /* STORY_ROOM04_USE_MIRROR_ALREADY */
        } else if (game_inventory_has_one(54u)) { /* Silver Shard */
            uint8_t slot;
            game_inventory_remove_one(54u);
            game_state.flags[18] = 1u; /* STORY_STATE_MIRROR_REPAIRED */
            game_mirror_repair_apply();
            (void)platform_room_object_add(&platform_room, 37u /* Mara Moonfen */,
                                           26u, 2u, &slot);
            (void)game_world_capture_current();
            platform_room_draw(&platform_room, platform_player);
            (void)game_room_script_entry(6u); /* STORY_ROOM04_REPAIR_MIRROR */
        } else {
            (void)game_room_script_entry(8u); /* STORY_ROOM04_USE_MIRROR_NO_ITEM */
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
