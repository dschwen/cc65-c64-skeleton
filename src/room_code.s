.setcpu "6502"

.export _game_room_enter_tile_native
.export _game_room_look_at_native

.import _game_room_code_active

ROOM_ENTER_TILE = $9900
ROOM_LOOK_AT    = $9903

.segment "HIGHCODE"

_game_room_enter_tile_native:
    lda _game_room_code_active
    beq @enter_done
    jsr ROOM_ENTER_TILE
@enter_done:
    rts

; fastcall: A = cardinal direction. Return A=0/1 and X=0.
_game_room_look_at_native:
    ldx _game_room_code_active
    beq @look_default
    jsr ROOM_LOOK_AT
    ldx #0
    rts
@look_default:
    lda #0
    tax
    rts
