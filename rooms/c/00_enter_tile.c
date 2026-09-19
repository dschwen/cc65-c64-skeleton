/* Room 00's enter_tile: keeps the mechanical tile-entry-count bump (what
 * the DSL's "flag ... increment" shape would produce), then adds the one
 * compound condition the DSL has no statement for - the "mysterious inn"
 * trigger: the Milestone has been looked at, the sequence hasn't already
 * fired, at least 20 turns have passed, and the player is standing on the
 * road (tile ID 22 or 23). Spliced by tools/compile_room.py's
 * `enter_tile: asm "path"` form (generic name substituted per room). */
void enter_tile(void) {
    uint8_t tile_x, tile_y, tile;

    ++game_state.flags[0]; /* STORY_STATE_ROOM_00_TILE_ENTRY_COUNT */

    if (!game_state.flags[2]) return;  /* STORY_STATE_ROOM_00_MILESTONE_SEEN */
    if (game_state.flags[3]) return;   /* STORY_STATE_ROOM_00_MYSTERY_TRIGGERED */
    if (game_state.turn < 20u) return;

    tile_x = game_state.player_x >> 1;
    tile_y = game_state.player_y >> 1;
    tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != 22u && tile != 23u) return;

    game_state.flags[3] = 1u; /* STORY_STATE_ROOM_00_MYSTERY_TRIGGERED */
    (void)game_room_script_entry(4u); /* STORY_ROOM00_MYSTERY_SEQUENCE */
}
