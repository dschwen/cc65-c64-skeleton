/* Fallback for room 03's use_at. Handles the kettle (11,6) - Lady Orra's
 * branch of the cellar-token puzzle (MOONFEN_STORY_DESIGN.md section 5):
 * holding the Blank Letter (type 56, given by Orra on first meeting - see
 * rooms/c/02_use_at.c) over the steaming kettle reveals it as the Orra
 * Invite (type 57), a type-swap rather than a separate flag, matching the
 * Long Taper -> Lit Taper convention (rooms/c/02_use_at.c). Already
 * holding the revealed invite, or holding neither, gets its own flavor
 * entry instead of silently doing nothing. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x != 11u || tile_y != 6u) return GAME_USE_DEFAULT;

    if (game_inventory_has_one(56u)) { /* Blank Letter */
        game_inventory_remove_one(56u);
        game_inventory_add_one(57u); /* Orra Invite */
        (void)game_room_script_entry(13u); /* STORY_ROOM03_USE_KETTLE */
    } else if (game_inventory_has_one(57u)) { /* Orra Invite */
        (void)game_room_script_entry(14u); /* STORY_ROOM03_USE_KETTLE_ALREADY */
    } else {
        (void)game_room_script_entry(15u); /* STORY_ROOM03_USE_KETTLE_NO_ITEM */
    }
    return GAME_USE_HANDLED;
}
