; Hand-written enter_tile for room 00: keeps the existing tile-entry-count
; bump (STORY_STATE_ROOM_00_TILE_ENTRY_COUNT, same as the DSL's mechanical
; "flag ... increment" shape used to produce), then adds the one compound
; condition the DSL has no statement for - the "mysterious inn" trigger:
; the Milestone has been looked at, the sequence hasn't already fired, at
; least 20 turns have passed, and the player is standing on the road (tile
; ID 22 or 23) - see the plan doc for why each of these is checked here in
; asm rather than via a .rc/.script mechanism. Spliced verbatim into the
; DSL-generated room-00 output by tools/compile_room.py
; (enter_tile: asm "rooms/asm/00_enter_tile.s").
.setcpu "6502"

.import _platform_room

.export _enter_tile

; GameState.flags[] base offset within _game_state (see
; tools/compile_room.py's GAME_STATE_FLAGS_OFFSET comment - re-check this if
; GameState's field order/size in src/game.h ever changes).
FLAGS_BASE = _game_state+84
TURN_LO    = _game_state+0     ; uint32_t turn, 4 bytes, little-endian
PLAYER_X   = _game_state+10    ; half-tile coordinates
PLAYER_Y   = _game_state+11
ROOM_TILES = _platform_room+13 ; 13-byte header precedes the tile grid

.segment "BSS"
tile_index: .res 1

.segment "CODE"

_enter_tile:
    inc FLAGS_BASE+0            ; STORY_STATE_ROOM_00_TILE_ENTRY_COUNT

    lda FLAGS_BASE+2            ; STORY_STATE_ROOM_00_MILESTONE_SEEN
    beq @done
    lda FLAGS_BASE+3            ; STORY_STATE_ROOM_00_MYSTERY_TRIGGERED
    bne @done

    ; turn >= 20: any of the upper 3 bytes nonzero, or the low byte >= 20
    lda TURN_LO+1
    ora TURN_LO+2
    ora TURN_LO+3
    bne @turn_ok
    lda TURN_LO+0
    cmp #20
    bcc @done
@turn_ok:

    ; tile_index = (player_y >> 1) * 20 + (player_x >> 1)
    lda PLAYER_Y
    lsr a
    asl a
    asl a
    sta tile_index               ; tile_y * 4
    asl a
    asl a                        ; tile_y * 16
    clc
    adc tile_index                ; tile_y*16 + tile_y*4 = tile_y*20
    sta tile_index
    lda PLAYER_X
    lsr a
    clc
    adc tile_index
    tax
    lda ROOM_TILES,x
    cmp #22
    beq @on_road
    cmp #23
    bne @done

@on_road:
    lda #1
    sta FLAGS_BASE+3             ; STORY_STATE_ROOM_00_MYSTERY_TRIGGERED
    lda #4                       ; STORY_ROOM00_MYSTERY_SEQUENCE
    jsr _game_room_script_entry

@done:
    rts
