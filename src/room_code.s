.setcpu "6502"

.export _game_room_enter_tile_native
.export _game_room_look_at_native
.export _game_room_enter_room_native

.import _game_room_code_active
.import incsp1

ROOM_ENTER_TILE = $9900
ROOM_LOOK_AT    = $9903
ROOM_ENTER_ROOM = $9906

.segment "HIGHCODE"

_game_room_enter_tile_native:
    lda _game_room_code_active
    beq @enter_done
    jsr ROOM_ENTER_TILE
@enter_done:
    rts

; fastcall: A = tile y, tile x is on the C stack. The active room handler has
; the same signature and therefore consumes the stacked byte itself.
_game_room_look_at_native:
    ldx _game_room_code_active
    beq @look_default
    jsr ROOM_LOOK_AT
    ldx #0
    rts
@look_default:
    lda #0
    tax
    jmp incsp1

.segment "UPPERCODE"

_game_room_enter_room_native:
    lda _game_room_code_active
    beq @room_done
    jsr ROOM_ENTER_ROOM
@room_done:
    rts
