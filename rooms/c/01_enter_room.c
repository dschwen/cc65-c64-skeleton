/* Room 01's enter_room: shows the one-time arrival narration the first
 * time this room is entered, gated on a dedicated flag rather than
 * game_entry_reason so a later save/load re-entry never re-shows it.
 * Spliced by tools/compile_room.py's `enter_room: asm "path"` form. */
void enter_room(void) {
    if (game_state.flags[4]) return; /* STORY_STATE_ROOM_01_ARRIVAL_SHOWN */
    game_state.flags[4] = 1u;
    (void)game_room_script_entry(1u); /* STORY_ROOM01_ARRIVAL */
}
