/* Fallback for room 04's use_at. Forwards to game_ignite_brazier(), which
 * scans the room's own objects for an unlit Brazier at this tile and, if
 * the player carries a Lit Taper, swaps it in place for a Lit Brazier.
 * That function's own return value is already GAME_USE_DEFAULT/HANDLED. */
uint8_t use_at_fallback(uint8_t tile_x, uint8_t tile_y) {
    return game_ignite_brazier(tile_x, tile_y);
}
