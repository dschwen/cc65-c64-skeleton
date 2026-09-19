/* Fallback for room 02's use_at. Handles the two tiles that need an
 * inventory check the script DSL cannot express:
 *   - (14,1), Sir Caldus: carrying Lamp Oil (type 51) consumes it and
 *     runs STORY_ROOM02_CALDUS_FREE; otherwise the ordinary
 *     STORY_ROOM02_TALK_CALDUS greeting/repeat entry.
 *   - (12,1), the hearth: carrying an unlit Long Taper (type 52) consumes
 *     it, adds a Lit Taper (type 53) and runs STORY_ROOM02_LIGHT_TAPER;
 *     already carrying a Lit Taper runs STORY_ROOM02_LIGHT_TAPER_ALREADY;
 *     otherwise STORY_ROOM02_LIGHT_TAPER_NO_ITEM. Lighting it also gives
 *     the player's own type a light radius directly via
 *     platform_hero_light_set(), then rebuilds lighting immediately. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 14u && tile_y == 1u) {
        if (game_inventory_has_one(51u)) { /* LAMP_OIL_TYPE */
            game_inventory_remove_one(51u);
            (void)game_room_script_entry(9u); /* STORY_ROOM02_CALDUS_FREE */
        } else {
            (void)game_room_script_entry(6u); /* STORY_ROOM02_TALK_CALDUS */
        }
        return GAME_USE_HANDLED;
    }

    if (tile_x == 12u && tile_y == 1u) {
        if (game_inventory_has_one(52u)) { /* LONG_TAPER_TYPE */
            game_inventory_remove_one(52u);
            game_inventory_add_one(53u); /* LIT_TAPER_TYPE */
            platform_hero_light_set(6u);
            platform_lighting_rebuild(&platform_room, platform_player);
            (void)game_room_script_entry(10u); /* STORY_ROOM02_LIGHT_TAPER */
        } else if (game_inventory_has_one(53u)) {
            (void)game_room_script_entry(13u); /* STORY_ROOM02_LIGHT_TAPER_ALREADY */
        } else {
            (void)game_room_script_entry(11u); /* STORY_ROOM02_LIGHT_TAPER_NO_ITEM */
        }
        return GAME_USE_HANDLED;
    }

    return GAME_USE_DEFAULT;
}
