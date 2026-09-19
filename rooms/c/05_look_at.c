/* Fallback for room 05's look_at. Tile 65 is the storm window - same
 * flavor-line pattern as 03_look_at.c. */
uint8_t look_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != 65u) return GAME_LOOK_DEFAULT;
    (void)game_room_script_entry(2u); /* STORY_ROOM05_LOOK_WINDOW */
    return GAME_LOOK_HANDLED;
}
