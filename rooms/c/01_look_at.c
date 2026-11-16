/* Fallback for room 01's look_at - runs whenever no `at X,Y -> script`
 * entry matched. Looking into the puddle (water tiles) from a distance is
 * "too muddy"; standing in it, the one tile at (3,4) hides the Blue Seal.
 * A C fragment because it compares the Look tile with the player's own. */
uint8_t look_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != STORY_TILE_WATER) return GAME_LOOK_DEFAULT;

    if ((game_state.player_x >> 1) != tile_x || (game_state.player_y >> 1) != tile_y) {
        (void)game_room_script_entry(STORY_ROOM01_SEARCH_FAR);
        return GAME_LOOK_HANDLED;
    }

    if (tile_x == 3u && tile_y == 4u) {
        (void)game_room_script_entry(STORY_ROOM01_FIND_SEAL);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}
