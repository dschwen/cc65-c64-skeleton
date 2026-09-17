; Fallback for room 04's use_at, spliced by tools/compile_room.py's
; `default -> asm "path"` support. Reached via a plain `jsr`, with the use
; target's tile_x/tile_y still sitting on the software stack exactly where
; the generated dispatch's own `jsr pusha` left them (offset 1 = tile_x,
; offset 0 = tile_y - see gen_dispatch()'s own ABI comment); must return its
; result in A (X is zeroed by the caller) using the same
; GAME_USE_DEFAULT/HANDLED convention a matched `at` entry would.
;
; Forwards to game_ignite_brazier() (src/game_support.c), which scans the
; room's own objects for an unlit Brazier (type 41) at this tile and, if
; the player carries a Lit Taper (type 53 - see rooms/asm/02_use_at.s),
; swaps it in place for a Lit Brazier (type 40). That function's own return
; value is already GAME_USE_DEFAULT/HANDLED, so it becomes ours directly -
; any tile without an unlit brazier (or without a lit taper in hand) falls
; through to the ordinary default via that same return value.
.setcpu "6502"

.import _game_ignite_brazier
.import pusha
.import incsp1
.importzp sp

.export _use_at_fallback

.segment "BSS"
use_fallback_x: .res 1
use_fallback_y: .res 1

.segment "CODE"

_use_at_fallback:
    ldy #1
    lda (sp),y
    sta use_fallback_x
    ldy #0
    lda (sp),y
    sta use_fallback_y

    lda use_fallback_x
    jsr pusha                    ; push tile_x, game_ignite_brazier's first arg
    lda use_fallback_y
    jsr _game_ignite_brazier     ; tile_y in A (fastcall's last arg); result in A
    pha
    jsr incsp1                   ; pop the tile_x pushed above for that call
    pla
    rts
