; Room 04's enter_tile: stepping onto the stairs-up tile (16,1, the
; already-walkable tile 67 - see assets/tiles.ctil) transitions back to the
; kitchen immediately, rather than requiring Use. Any other tile is a
; no-op. Spliced by tools/compile_room.py's `enter_tile: asm "path"` form;
; exports _enter_tile, called with no arguments, returns nothing (rts).
.setcpu "6502"

.import _game_state
.import _game_room_script_entry

.export _enter_tile

PLAYER_X = _game_state+10
PLAYER_Y = _game_state+11
STAIRS_X = 16
STAIRS_Y = 1

.segment "CODE"

_enter_tile:
    lda PLAYER_X
    lsr a
    cmp #STAIRS_X
    bne @done
    lda PLAYER_Y
    lsr a
    cmp #STAIRS_Y
    bne @done

    lda #1                      ; STORY_ROOM04_ASCEND_STAIRS
    jsr _game_room_script_entry
@done:
    rts
