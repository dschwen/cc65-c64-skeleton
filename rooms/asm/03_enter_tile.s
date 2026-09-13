; Room 03's enter_tile: stepping onto the stairs-down tile (3,5, tile ID 68
; - a walkable clone of the decorative stairs tile 65 used elsewhere in
; this room, see assets/tiles.ctil) transitions to the basement
; immediately, rather than requiring Use. Any other tile is a no-op.
; Spliced by tools/compile_room.py's `enter_tile: asm "path"` form; exports
; _enter_tile, called with no arguments, returns nothing (rts).
.setcpu "6502"

.import _game_state
.import _game_room_script_entry

.export _enter_tile

PLAYER_X = _game_state+10
PLAYER_Y = _game_state+11
STAIRS_X = 3
STAIRS_Y = 5

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

    lda #7                      ; STORY_ROOM03_DESCEND_STAIRS
    jsr _game_room_script_entry
@done:
    rts
