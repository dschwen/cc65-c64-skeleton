; Fallback for room 01's look_at, spliced by tools/compile_room.py's
; `default -> asm "path"` support (see its docstring) - runs whenever none
; of the room's declared `at X,Y -> script` entries matched (this room has
; none, so this always runs). Reached via a plain `jsr`, with the look
; target's tile_x/tile_y still sitting on the software stack exactly where
; the generated dispatch's own `jsr pusha` left them (offset 1 = tile_x,
; offset 0 = tile_y - see gen_dispatch()'s own ABI comment); must return its
; result in A (X is zeroed by the caller) using the same
; GAME_LOOK_DEFAULT/HANDLED convention a matched `at` entry would.
;
; Rules, checked in this order:
;   - target is water (tile ID 2) and the player is NOT standing on the
;     target tile -> STORY_ROOM01_SEARCH_FAR ("too muddy, get closer")
;   - target is water, the player IS standing on it, and it's exactly
;     (3,4) -> STORY_ROOM01_FIND_SEAL
;   - target is water, the player IS standing on it, but it isn't (3,4), or
;     the target isn't water at all -> falls through to the ordinary
;     default (no special text).
.setcpu "6502"

.import _game_state
.import _platform_room
.import _game_room_script_entry
.importzp sp

.export _look_at_fallback

LOOK_PLAYER_X   = _game_state+10
LOOK_PLAYER_Y   = _game_state+11
LOOK_ROOM_TILES = _platform_room+13
LOOK_WATER_TILE = 2
LOOK_SEAL_X = 3
LOOK_SEAL_Y = 4

.segment "BSS"
look_fallback_x:     .res 1
look_fallback_y:     .res 1
look_fallback_index: .res 1

.segment "CODE"

_look_at_fallback:
    ldy #1
    lda (sp),y
    sta look_fallback_x
    ldy #0
    lda (sp),y
    sta look_fallback_y

    ; tile_index = target_y*20 + target_x
    lda look_fallback_y
    asl a
    asl a
    sta look_fallback_index
    asl a
    asl a
    clc
    adc look_fallback_index
    clc
    adc look_fallback_x
    tax
    lda LOOK_ROOM_TILES,x
    cmp #LOOK_WATER_TILE
    bne @default

    lda LOOK_PLAYER_X
    lsr a
    cmp look_fallback_x
    bne @different_tile
    lda LOOK_PLAYER_Y
    lsr a
    cmp look_fallback_y
    bne @different_tile

    lda look_fallback_x
    cmp #LOOK_SEAL_X
    bne @default
    lda look_fallback_y
    cmp #LOOK_SEAL_Y
    bne @default
    lda #3                      ; STORY_ROOM01_FIND_SEAL
    jsr _game_room_script_entry
    lda #1                      ; GAME_LOOK_HANDLED
    rts

@different_tile:
    lda #4                      ; STORY_ROOM01_SEARCH_FAR
    jsr _game_room_script_entry
    lda #1                      ; GAME_LOOK_HANDLED
    rts

@default:
    lda #0                      ; GAME_LOOK_DEFAULT
    rts
