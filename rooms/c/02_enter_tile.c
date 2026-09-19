/* Room 02's enter_tile: the two open doorways in the top wall (tile ID 5,
 * already walkable) transition the instant the player steps onto them -
 * (2,0) behind the bar into the kitchen, (15,0) beside the hearth into
 * the guest corridor. Any other tile is a no-op. */
void enter_tile(void) {
    uint8_t tile_x = game_state.player_x >> 1;
    uint8_t tile_y = game_state.player_y >> 1;

    if (tile_y != 0u) return;
    if (tile_x == 2u) {
        (void)game_room_script_entry(8u); /* STORY_ROOM02_ENTER_KITCHEN */
    } else if (tile_x == 15u) {
        (void)game_room_script_entry(12u); /* STORY_ROOM02_ENTER_CORRIDOR */
    }
}
