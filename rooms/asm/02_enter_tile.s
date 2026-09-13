; Room 02's enter_tile: the two open doorways in the top wall (tile ID 5,
; already walkable) transition the instant the player steps onto them,
; rather than requiring Use - (2,0) behind the bar into the kitchen,
; (15,0) beside the hearth into the guest corridor. Any other tile is a
; no-op. Spliced by tools/compile_room.py's `enter_tile: asm "path"` form;
; exports _enter_tile, called with no arguments, returns nothing (rts).
.setcpu "6502"

.import _game_state
.import _game_room_script_entry

.export _enter_tile

PLAYER_X = _game_state+10
PLAYER_Y = _game_state+11
DOOR_Y = 0
KITCHEN_DOOR_X  = 2
CORRIDOR_DOOR_X = 15

.segment "CODE"

_enter_tile:
    lda PLAYER_Y
    lsr a
    cmp #DOOR_Y
    bne @done

    lda PLAYER_X
    lsr a
    cmp #KITCHEN_DOOR_X
    beq @kitchen
    cmp #CORRIDOR_DOOR_X
    beq @corridor
    rts

@kitchen:
    lda #8                      ; STORY_ROOM02_ENTER_KITCHEN
    jsr _game_room_script_entry
    rts

@corridor:
    lda #12                     ; STORY_ROOM02_ENTER_CORRIDOR
    jsr _game_room_script_entry
    rts

@done:
    rts
