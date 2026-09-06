; Hand-written enter_room for room 01: shows the one-time arrival narration
; ("Suddenly a mysterious inn appears...") the first time this room is
; entered, gated on a dedicated flag rather than game_entry_reason so a
; later save/load re-entry never re-shows it. Must run from here rather
; than from room 00's own script, because the transition that lands the
; player here applies inside a screen-blanked bracket that clears the
; bottom text rows as part of the switch - see PLATFORM_API.md and
; src/game.c's game_process_pending_transition() comment. Spliced verbatim
; into the DSL-generated room-01 output by tools/compile_room.py
; (enter_room: asm "rooms/asm/01_enter_room.s").
.setcpu "6502"

.export _enter_room

FLAGS_BASE = _game_state+84

.segment "CODE"

_enter_room:
    lda FLAGS_BASE+4             ; STORY_STATE_ROOM_01_ARRIVAL_SHOWN
    bne @done
    lda #1
    sta FLAGS_BASE+4
    lda #1                       ; STORY_ROOM01_ARRIVAL
    jsr _game_room_script_entry
@done:
    rts
