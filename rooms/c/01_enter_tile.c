/* Room 01's enter_tile: keeps the tile-entry-count bump, then swaps the
 * player's own object type to 0x31 ("submerged hero") while standing on a
 * water tile (tile ID 2), and back to 0x01 ("The Hero") otherwise. Runs
 * unconditionally every tile-entry event; relies on
 * platform_player_set_type()'s own no-op-if-unchanged check. */
void enter_tile(void) {
    uint8_t tile_x, tile_y, tile;

    ++game_state.flags[1]; /* STORY_STATE_ROOM_01_TILE_ENTRY_COUNT */

    tile_x = game_state.player_x >> 1;
    tile_y = game_state.player_y >> 1;
    tile = platform_room.tiles[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x];

    platform_player_set_type(tile == 2u ? 0x31u : 0x01u);
}
