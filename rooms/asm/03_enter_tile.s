; Room 03's enter_tile: stepping onto either of two tiles transitions
; immediately, rather than requiring Use:
;   - (10,1), the stairs down (the already-walkable tile 10 - see
;     assets/tiles.ctil) - transitions to the basement.
;   - (6,10), the real door back to the tavern (the already-walkable
;     door tile 5 in the south wall - the same tile graphic room 02/05's
;     own top-wall doors use). The player arrives from the tavern one
;     tile north of it, at (6,9), so stepping onto (6,10) is always a
;     deliberate move back through the door, never the landing tile
;     itself (which would otherwise immediately bounce back out).
; Any other tile is a no-op. Spliced by tools/compile_room.py's
; `enter_tile: asm "path"` form; exports _enter_tile, called with no
; arguments, returns nothing (rts).
.setcpu "6502"

.import _game_state
.import _game_room_script_entry

.export _enter_tile

PLAYER_X = _game_state+10
PLAYER_Y = _game_state+11
STAIRS_X = 10
STAIRS_Y = 1
DOOR_X = 6
DOOR_Y = 10

.segment "CODE"

_enter_tile:
    lda PLAYER_X
    lsr a
    sta tile_x
    lda PLAYER_Y
    lsr a
    sta tile_y

    lda tile_x
    cmp #STAIRS_X
    bne @check_door
    lda tile_y
    cmp #STAIRS_Y
    bne @check_door

    lda #7                      ; STORY_ROOM03_DESCEND_STAIRS
    jsr _game_room_script_entry
    rts

@check_door:
    lda tile_x
    cmp #DOOR_X
    bne @done
    lda tile_y
    cmp #DOOR_Y
    bne @done

    lda #1                      ; STORY_ROOM03_LEAVE_KITCHEN
    jsr _game_room_script_entry
@done:
    rts

.segment "BSS"
tile_x: .res 1
tile_y: .res 1
