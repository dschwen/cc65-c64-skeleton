/* Room 05's enter_tile: stepping onto the south-wall doorway (2,10)
 * transitions back to the tavern immediately, rather than requiring
 * Use. */
void enter_tile(void) {
    uint8_t tile_x = game_state.player_x >> 1;
    uint8_t tile_y = game_state.player_y >> 1;

    if (tile_x == 2u && tile_y == 10u) {
        (void)game_room_script_entry(1u); /* STORY_ROOM05_LEAVE_CORRIDOR */
    }
}
