/* Fallback for room 03's look_at. Tile 65 is the storm window - wherever
 * it appears in this room's tile grid, looking at it always shows the
 * same weather flavor line. */
uint8_t look_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != 65u) return GAME_LOOK_DEFAULT;
    (void)game_room_script_entry(9u); /* STORY_ROOM03_LOOK_WINDOW */
    return GAME_LOOK_HANDLED;
}
