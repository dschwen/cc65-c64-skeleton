; Fallback for room 05's look_at, spliced by tools/compile_room.py's
; `default -> asm "path"` support. Reached via a plain `jsr`, with the look
; target's tile_x/tile_y still sitting on the software stack exactly where
; the generated dispatch's own `jsr pusha` left them (offset 1 = tile_x,
; offset 0 = tile_y - see gen_dispatch()'s own ABI comment); must return its
; result in A (X is zeroed by the caller) using the same
; GAME_LOOK_DEFAULT/HANDLED convention a matched `at` entry would.
;
; Tile 65 is the storm window - wherever it appears in this room's tile
; grid, looking at it always shows the same weather flavor line
; (STORY_ROOM05_LOOK_WINDOW), rather than needing an `at X,Y` entry per
; placement. Any other tile falls through to the ordinary default (the
; three decorative guest doors have their own explicit `at` entries in
; rooms/05.rc, ahead of this fallback).
.setcpu "6502"

.import _platform_room
.import _game_room_script_entry
.importzp sp

.export _look_at_fallback

WINDOW_TILE = 65
ROOM_TILES = _platform_room+13

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
    lda ROOM_TILES,x
    cmp #WINDOW_TILE
    bne @default

    lda #2                       ; STORY_ROOM05_LOOK_WINDOW
    jsr _game_room_script_entry
    lda #1                       ; GAME_LOOK_HANDLED
    rts

@default:
    lda #0                       ; GAME_LOOK_DEFAULT
    rts
