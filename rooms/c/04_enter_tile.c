/* Room 04's enter_tile: stepping onto the stairs-up tile (16,1)
 * transitions back to the kitchen immediately, rather than requiring
 * Use. */
void enter_tile(void) {
    uint8_t tile_x = game_state.player_x >> 1;
    uint8_t tile_y = game_state.player_y >> 1;

    if (tile_x == 16u && tile_y == 1u) {
        (void)game_room_script_entry(1u); /* STORY_ROOM04_ASCEND_STAIRS */
    }
}
