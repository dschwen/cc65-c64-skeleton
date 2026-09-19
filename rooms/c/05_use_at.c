/* Fallback for room 05's use_at. Handles Pell Thimble's tile (4,7) - the
 * Blue Seal check the script DSL cannot express:
 *   - already helped (STORY_STATE_ROOM_05_PELL_HELPED set): always just
 *     talk (the "thanks again" repeat text handles itself via
 *     check_flag).
 *   - not yet helped, carrying the Blue Seal (type 43): both evidence
 *     flags set (invitation read, blue door examined) -> consume the
 *     seal and run STORY_ROOM05_PELL_RESOLVE; otherwise
 *     STORY_ROOM05_PELL_EARLY_SEAL (refusal, seal kept).
 *   - not carrying the seal: talk (STORY_ROOM05_TALK_PELL). */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x != 4u || tile_y != 7u) return GAME_USE_DEFAULT;

    if (game_state.flags[13]) { /* STORY_STATE_ROOM_05_PELL_HELPED */
        (void)game_room_script_entry(6u); /* STORY_ROOM05_TALK_PELL */
        return GAME_USE_HANDLED;
    }

    if (game_inventory_has_one(43u)) { /* BLUE_SEAL_TYPE */
        if (game_state.flags[11] && game_state.flags[12]) {
            /* STORY_STATE_ROOM_05_PELL_INVITE_SEEN, _BLUE_DOOR_SEEN */
            game_inventory_remove_one(43u);
            (void)game_room_script_entry(10u); /* STORY_ROOM05_PELL_RESOLVE */
        } else {
            (void)game_room_script_entry(9u); /* STORY_ROOM05_PELL_EARLY_SEAL */
        }
    } else {
        (void)game_room_script_entry(6u); /* STORY_ROOM05_TALK_PELL */
    }
    return GAME_USE_HANDLED;
}
