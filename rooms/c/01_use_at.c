/* Fallback for room 01's use_at:
 *   - the rain barrel (5,8): carrying the Empty Cup (type 58) fills it -
 *     consumes the cup, gives Rainwater (type 59), one of the three
 *     broth ingredients (MOONFEN_STORY_DESIGN.md section 7). Already
 *     carrying Rainwater or carrying neither gets its own flavor entry.
 *   - otherwise, the same water/seal logic as 01_look_at.c's
 *     look_at_fallback, for the Use cursor. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile;

    if (tile_x == 5u && tile_y == 8u) {
        if (game_inventory_has_one(58u)) { /* Empty Cup */
            game_inventory_remove_one(58u);
            game_inventory_add_one(59u); /* Rainwater */
            (void)game_room_script_entry(5u); /* STORY_ROOM01_USE_BARREL_FILL */
        } else if (game_inventory_has_one(59u)) { /* Rainwater */
            (void)game_room_script_entry(6u); /* STORY_ROOM01_USE_BARREL_ALREADY */
        } else {
            (void)game_room_script_entry(7u); /* STORY_ROOM01_USE_BARREL_NO_ITEM */
        }
        return GAME_USE_HANDLED;
    }

    tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != 2u) return GAME_USE_DEFAULT;

    if ((game_state.player_x >> 1) != tile_x || (game_state.player_y >> 1) != tile_y) {
        (void)game_room_script_entry(4u); /* STORY_ROOM01_SEARCH_FAR */
        return GAME_USE_HANDLED;
    }

    if (tile_x == 3u && tile_y == 4u) {
        (void)game_room_script_entry(3u); /* STORY_ROOM01_FIND_SEAL */
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
