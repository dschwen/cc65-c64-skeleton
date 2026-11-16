/* Room 00's enter_tile: the tile-entry-count bump, then the one condition
 * the script DSL has no statement for - the "mysterious inn" trigger
 * reads the turn counter and the tile under the player: the Milestone has
 * been looked at, the sequence hasn't already fired, enough turns have
 * passed, and the player is standing on the road. */
void enter_tile(void) {
    uint8_t tile_x, tile_y, tile;

    ++game_state.flags[STORY_STATE_ROOM_00_TILE_ENTRY_COUNT];

    if (!game_state.flags[STORY_STATE_ROOM_00_MILESTONE_SEEN]) return;
    if (game_state.flags[STORY_STATE_ROOM_00_MYSTERY_TRIGGERED]) return;
    if (game_state.turn < STORY_ROOM00_MYSTERY_MIN_TURNS) return;

    tile_x = game_state.player_x >> 1;
    tile_y = game_state.player_y >> 1;
    tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != STORY_TILE_ROAD_A && tile != STORY_TILE_ROAD_B) return;

    game_state.flags[STORY_STATE_ROOM_00_MYSTERY_TRIGGERED] = 1u;
    (void)game_room_script_entry(STORY_ROOM00_MYSTERY_SEQUENCE);
}
