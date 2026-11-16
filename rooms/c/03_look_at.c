/* Fallback for room 03's look_at: wherever the storm-window tile appears
 * in this room's grid, looking at it shows the same weather line. A C
 * fragment because it reads the tile's ID rather than a coordinate. */
uint8_t look_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    uint8_t tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];
    if (tile != STORY_TILE_STORM_WINDOW) return GAME_LOOK_DEFAULT;
    (void)game_room_script_entry(STORY_ROOM03_LOOK_WINDOW);
    return GAME_LOOK_HANDLED;
}
