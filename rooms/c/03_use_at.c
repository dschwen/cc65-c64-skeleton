/* Fallback for room 03's use_at. Handles two tiles that need an
 * inventory check the script DSL cannot express:
 *   - the kettle (11,6) - Lady Orra's branch of the cellar-token puzzle
 *     (MOONFEN_STORY_DESIGN.md section 5): holding the Blank Letter (type
 *     56, given by Orra on first meeting - see rooms/c/02_use_at.c) over
 *     the steaming kettle reveals it as the Orra Invite (type 57), a
 *     type-swap rather than a separate flag, matching the Long Taper ->
 *     Lit Taper convention (rooms/c/02_use_at.c). Already holding the
 *     revealed invite, or holding neither, gets its own flavor entry
 *     instead of silently doing nothing.
 *   - the cauldron (10,6) - the Guest's Broth puzzle (section 7): holding
 *     all three ingredients (Moonmint type 42, Rainwater type 59, Hearth
 *     Ash type 60 - rooms/c/01_use_at.c and 02_use_at.c) consumes them
 *     and gives the Guest's Broth (type 61); missing any of the three
 *     gets a flavor entry instead. There is no separate "ready" flag -
 *     same convention as every other synthesized item this pass, the
 *     Broth's own presence in inventory *is* the ready state (the finale,
 *     a later step, checks for it directly). */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 11u && tile_y == 6u) {
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

    if (tile_x == 10u && tile_y == 6u) {
        if (game_inventory_has_one(42u) &&  /* Moonmint */
            game_inventory_has_one(59u) &&  /* Rainwater */
            game_inventory_has_one(60u)) {  /* Hearth Ash */
            game_inventory_remove_one(42u);
            game_inventory_remove_one(59u);
            game_inventory_remove_one(60u);
            game_inventory_add_one(61u); /* Guest's Broth */
            (void)game_room_script_entry(16u); /* STORY_ROOM03_USE_CAULDRON_COOK */
        } else {
            (void)game_room_script_entry(17u); /* STORY_ROOM03_USE_CAULDRON_MISSING */
        }
        return GAME_USE_HANDLED;
    }

    return GAME_USE_DEFAULT;
}
