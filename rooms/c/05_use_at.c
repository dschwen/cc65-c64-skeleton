/* Fallback for room 05's use_at. Handles Pell Thimble's tile (4,7) - the
 * Blue Seal check the script DSL cannot express:
 *   - already helped (STORY_STATE_ROOM_05_PELL_HELPED set): always just
 *     talk (the "thanks again" repeat text handles itself via
 *     check_flag).
 *   - not yet helped, carrying the Blue Seal (type 43): Using it here no
 *     longer resolves anything directly - just a redirect hint
 *     (STORY_ROOM05_PELL_SEAL_HINT) pointing at conversation instead. The
 *     actual resolution (including the two-evidence-flags gate - the
 *     invitation read, the blue door examined) happens by mentioning
 *     "seal" while talking to him (story_conversation_topic(),
 *     story/story.c, checked from modules/script.c's run_conversation()
 *     before its own topic lookup); the seal is never consumed here.
 *   - not carrying the seal: talk (STORY_ROOM05_TALK_PELL), which itself
 *     leads into that same conversation via `converse`. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x != 4u || tile_y != 7u) return GAME_USE_DEFAULT;

    if (game_state.flags[13]) { /* STORY_STATE_ROOM_05_PELL_HELPED */
        (void)game_room_script_entry(6u); /* STORY_ROOM05_TALK_PELL */
        return GAME_USE_HANDLED;
    }

    if (game_inventory_has_one(43u)) { /* BLUE_SEAL_TYPE */
        (void)game_room_script_entry(9u); /* STORY_ROOM05_PELL_SEAL_HINT */
    } else {
        (void)game_room_script_entry(6u); /* STORY_ROOM05_TALK_PELL */
    }
    return GAME_USE_HANDLED;
}
