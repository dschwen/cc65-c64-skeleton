/* Room 01's enter_tile: the tile-entry-count bump, then a cosmetic swap of
 * the player's own object type to the Submerged Hero while standing in
 * the puddle, and back to The Hero otherwise (platform_player_set_type()
 * is a no-op if unchanged). A C fragment because it reads the tile under
 * the player, which the script DSL cannot. */
void enter_tile(void) {
    uint8_t tile_x, tile_y, tile;

    ++game_state.flags[STORY_STATE_ROOM_01_TILE_ENTRY_COUNT];

    tile_x = game_state.player_x >> 1;
    tile_y = game_state.player_y >> 1;
    tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];

    platform_player_set_type(tile == STORY_TILE_WATER ? OBJ_SUBMERGED_HERO
                                                      : OBJ_THE_HERO);
}
