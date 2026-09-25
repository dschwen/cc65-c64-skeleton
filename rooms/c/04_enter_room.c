/* Room 04's enter_room: re-applies the moon mirror's repaired appearance
 * on every arrival (game_mirror_repair_apply(), src/game_support.c) - a
 * no-op unless the mirror has already been repaired. Needed because the
 * mirror's bright color lives in the shared runtime tile_data table,
 * which modules/saveload.c reuses as scratch space and reloads from ROM
 * on every save/load, unlike the mirror's repaired *state* (the flag,
 * and Mara's presence as a room object) which the normal world-state
 * cache already restores correctly on its own. */
void enter_room(void) {
    game_mirror_repair_apply();
}
