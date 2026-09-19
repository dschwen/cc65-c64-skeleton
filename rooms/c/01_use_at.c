/* Fallback for room 01's use_at - same water/seal logic as
 * 01_look_at.c's look_at_fallback, for the Use cursor. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
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
