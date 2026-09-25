/* Room 03's enter_tile: stepping onto either of two tiles transitions
 * immediately, rather than requiring Use:
 *   - (10,1), the stairs down -> basement. Unwarded - the real cellar
 *     gate is a brass-key-locked door inside the basement itself
 *     (rooms/c/04_use_at.c), not a check at these stairs.
 *   - (6,10), the real door back to the tavern. */
void enter_tile(void) {
    uint8_t tile_x = game_state.player_x >> 1;
    uint8_t tile_y = game_state.player_y >> 1;

    if (tile_x == 10u && tile_y == 1u) {
        (void)game_room_script_entry(7u); /* STORY_ROOM03_DESCEND_STAIRS */
    } else if (tile_x == 6u && tile_y == 10u) {
        (void)game_room_script_entry(1u); /* STORY_ROOM03_LEAVE_KITCHEN */
    }
}
