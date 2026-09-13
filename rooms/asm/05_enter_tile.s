; Room 05's enter_tile: stepping onto the south-wall doorway (10,10)
; transitions back to the tavern immediately, rather than requiring Use.
; Any other tile is a no-op. Spliced by tools/compile_room.py's
; `enter_tile: asm "path"` form; exports _enter_tile, called with no
; arguments, returns nothing (rts).
.setcpu "6502"

.import _game_state
.import _game_room_script_entry

.export _enter_tile

PLAYER_X = _game_state+10
PLAYER_Y = _game_state+11
DOOR_X = 10
DOOR_Y = 10

.segment "CODE"

_enter_tile:
    lda PLAYER_X
    lsr a
    cmp #DOOR_X
    bne @done
    lda PLAYER_Y
    lsr a
    cmp #DOOR_Y
    bne @done

    lda #1                      ; STORY_ROOM05_LEAVE_CORRIDOR
    jsr _game_room_script_entry
@done:
    rts
