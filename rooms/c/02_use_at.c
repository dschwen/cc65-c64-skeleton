/* Fallback for room 02's use_at. Handles the three tiles that need an
 * inventory check the script DSL cannot express:
 *   - (14,1), Sir Caldus: carrying Lamp Oil (type 51) consumes it and
 *     runs STORY_ROOM02_CALDUS_FREE; otherwise the ordinary
 *     STORY_ROOM02_TALK_CALDUS greeting/repeat entry.
 *   - (12,1), the hearth: carrying an unlit Long Taper (type 52) consumes
 *     it, adds a Lit Taper (type 53) and runs STORY_ROOM02_LIGHT_TAPER;
 *     already carrying a Lit Taper runs STORY_ROOM02_LIGHT_TAPER_ALREADY;
 *     lighting also gives the player's own type a light radius directly
 *     via platform_hero_light_set(), then rebuilds lighting immediately.
 *     Carrying neither, and not yet holding Hearth Ash (type 60, the
 *     third Guest's Broth ingredient - MOONFEN_STORY_DESIGN.md section 7),
 *     collects a scoop of it from the embers (STORY_ROOM02_COLLECT_ASH);
 *     already holding it falls back to the plain STORY_ROOM02_
 *     LIGHT_TAPER_NO_ITEM flavor, since there's nothing further to do here.
 *   - (17,9), Lady Orra: carrying the revealed Orra Invite (type 57,
 *     rooms/c/03_use_at.c's kettle steam reveal) consumes it and runs
 *     STORY_ROOM02_ORRA_RESOLVE; otherwise the ordinary
 *     STORY_ROOM02_TALK_ORRA greeting/repeat entry (which itself gives
 *     the Blank Letter on first meeting, script-side - see
 *     assets/scripts/02.script).
 *   - (15,4), the dining table: carrying the Guest's Broth (type 61,
 *     MOONFEN_STORY_DESIGN.md section 10) consumes it and sets
 *     STORY_STATE_BROTH_SERVED; already served or not carrying it gets
 *     its own flavor entry. The service bell (2,3) is a plain state-flag
 *     check the script DSL *can* express on its own - see
 *     STORY_ROOM02_RING_BELL in assets/scripts/02.script, dispatched
 *     directly from rooms/02.rc rather than from here. */
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
        } else if (!game_inventory_has_one(60u)) { /* Hearth Ash not yet collected */
            game_inventory_add_one(60u); /* Hearth Ash */
            (void)game_room_script_entry(15u); /* STORY_ROOM02_COLLECT_ASH */
        } else {
            (void)game_room_script_entry(11u); /* STORY_ROOM02_LIGHT_TAPER_NO_ITEM */
        }
        return GAME_USE_HANDLED;
    }

    if (tile_x == 17u && tile_y == 9u) {
        if (game_inventory_has_one(57u)) { /* Orra Invite */
            game_inventory_remove_one(57u);
            (void)game_room_script_entry(14u); /* STORY_ROOM02_ORRA_RESOLVE */
        } else {
            (void)game_room_script_entry(2u); /* STORY_ROOM02_TALK_ORRA */
        }
        return GAME_USE_HANDLED;
    }

    if (tile_x == 15u && tile_y == 4u) {
        if (game_state.flags[20]) { /* STORY_STATE_BROTH_SERVED */
            (void)game_room_script_entry(17u); /* STORY_ROOM02_SERVE_BROTH_ALREADY */
        } else if (game_inventory_has_one(61u)) { /* Guest's Broth */
            game_inventory_remove_one(61u);
            (void)game_room_script_entry(16u); /* STORY_ROOM02_SERVE_BROTH */
        } else {
            (void)game_room_script_entry(18u); /* STORY_ROOM02_SERVE_BROTH_NO_ITEM */
        }
        return GAME_USE_HANDLED;
    }

    return GAME_USE_DEFAULT;
}
