/* Room 04's enter_room: re-applies two runtime patches on every arrival,
 * neither of which the normal world-state object-delta cache covers (it
 * only tracks room->objects[], never room->tiles[] or the shared
 * tile_data table - confirmed in src/world.c - and platform_room_enter()
 * reloads a room's tiles fresh from ROM on *every* entry, not just after
 * a save/load):
 *   - the moon mirror's repaired appearance (game_mirror_repair_apply(),
 *     src/game_support.c) - a no-op unless already repaired.
 *   - the entry alcove door (18,4), swapped from its locked-door tile to
 *     plain floor when unlocked (rooms/c/04_use_at.c) - re-swap it here
 *     too, or leaving and returning would silently re-lock it even
 *     though STORY_STATE_BASEMENT_DOOR_OPEN (and the Brass Key already
 *     spent reaching it) says otherwise. */
void enter_room(void) {
    game_mirror_repair_apply();
    if (game_state.flags[14]) { /* STORY_STATE_BASEMENT_DOOR_OPEN */
        platform_room.tiles[4u * PLATFORM_MAP_WIDTH + 18u] = 11u;
    }
}
