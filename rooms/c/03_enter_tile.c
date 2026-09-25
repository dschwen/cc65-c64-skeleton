/* Room 03's enter_tile: stepping onto either of two tiles transitions
 * immediately, rather than requiring Use:
 *   - (10,1), the stairs down -> basement, warded shut until the player
 *     carries all three cellar tokens (MOONFEN_STORY_DESIGN.md section 5) -
 *     the script DSL has no opcode to check inventory, so the check lives
 *     here rather than as a plain `at X,Y` entry. Non-destructive: the
 *     tokens are not consumed opening the ward (the Silver Shard is still
 *     needed for the moon mirror afterward), and STORY_STATE_CELLAR_OPEN
 *     is checked first so the ward, once satisfied, stays open even if a
 *     token later leaves the inventory (e.g. the shard, into the mirror).
 *   - (6,10), the real door back to the tavern. */
void enter_tile(void) {
    uint8_t tile_x = game_state.player_x >> 1;
    uint8_t tile_y = game_state.player_y >> 1;

    if (tile_x == 10u && tile_y == 1u) {
        if (game_state.flags[14]) { /* STORY_STATE_CELLAR_OPEN */
            (void)game_room_script_entry(7u); /* STORY_ROOM03_DESCEND_STAIRS */
        } else if (game_inventory_has_one(10u) /* Brass Key */ &&
                   game_inventory_has_one(54u) /* Silver Shard */ &&
                   game_inventory_has_one(55u) /* Moon Brooch */) {
            (void)game_room_script_entry(10u); /* STORY_ROOM03_WARD_OPENED */
            (void)game_room_script_entry(7u);  /* STORY_ROOM03_DESCEND_STAIRS */
        } else {
            (void)game_room_script_entry(11u); /* STORY_ROOM03_WARD_BLOCKED */
        }
    } else if (tile_x == 6u && tile_y == 10u) {
        (void)game_room_script_entry(1u); /* STORY_ROOM03_LEAVE_KITCHEN */
    }
}
