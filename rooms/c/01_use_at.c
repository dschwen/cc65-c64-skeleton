/* Fallback for room 01's use_at: the same puddle/seal logic as
 * 01_look_at.c's look_at_fallback, for the Use cursor. (The rain barrel
 * is a plain `at 5,8 -> script` dispatch in rooms/01.rc.) */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != STORY_TILE_WATER) return GAME_USE_DEFAULT;

    if ((game_state.player_x >> 1) != tile_x || (game_state.player_y >> 1) != tile_y) {
        (void)game_room_script_entry(STORY_ROOM01_SEARCH_FAR);
        return GAME_USE_HANDLED;
    }

    if (tile_x == 3u && tile_y == 4u) {
        (void)game_room_script_entry(STORY_ROOM01_FIND_SEAL);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
