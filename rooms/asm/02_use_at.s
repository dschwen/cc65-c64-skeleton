; Fallback for room 02's use_at, spliced by tools/compile_room.py's
; `default -> asm "path"` support. Reached via a plain `jsr`, with the use
; target's tile_x/tile_y still sitting on the software stack exactly where
; the generated dispatch's own `jsr pusha` left them (offset 1 = tile_x,
; offset 0 = tile_y - see gen_dispatch()'s own ABI comment); must return its
; result in A (X is zeroed by the caller) using the same
; GAME_USE_DEFAULT/HANDLED convention a matched `at` entry would.
;
; Handles the two tiles that need an inventory check the script DSL cannot
; express (no "has item"/"consume item" opcode - see game_inventory_has_one/
; game_inventory_remove_one in src/game_support.c). Both tiles used to be
; plain `at X,Y -> script` entries in rooms/02.rc; they moved here so the
; inventory check can pick which script entry runs:
;   - (14,1), Sir Caldus's own tile: if the player carries Lamp Oil (object
;     type 51), consume one and run STORY_ROOM02_CALDUS_FREE; otherwise run
;     the ordinary STORY_ROOM02_TALK_CALDUS greeting/repeat entry.
;   - (12,1), the hearth: if the player carries a Long Taper (object type
;     52), light it (STORY_ROOM02_LIGHT_TAPER, which itself checks the
;     QUEST_FLAGS taper-lit bit for a repeat visit); otherwise
;     STORY_ROOM02_LIGHT_TAPER_NO_ITEM.
; Any other tile falls through to the ordinary default.
.setcpu "6502"

.import _game_room_script_entry
.import _game_inventory_has_one
.import _game_inventory_remove_one
.importzp sp

.export _use_at_fallback

LAMP_OIL_TYPE   = 51
LONG_TAPER_TYPE = 52
HEARTH_X = 12
HEARTH_Y = 1
CALDUS_X = 14
CALDUS_Y = 1

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
    cmp #CALDUS_X
    bne @check_hearth
    lda use_fallback_y
    cmp #CALDUS_Y
    bne @check_hearth

    lda #LAMP_OIL_TYPE
    jsr _game_inventory_has_one
    cmp #0                       ; Z from the call is unreliable: cc65's
                                  ; own incspN cleanup after a __fastcall__
                                  ; return re-touches the flags via its own
                                  ; internal inc, so re-test A explicitly
                                  ; rather than trusting the jsr's Z.
    beq @no_oil
    lda #LAMP_OIL_TYPE
    jsr _game_inventory_remove_one
    lda #9                      ; STORY_ROOM02_CALDUS_FREE
    jsr _game_room_script_entry
    lda #1                      ; GAME_USE_HANDLED
    rts

@no_oil:
    lda #6                      ; STORY_ROOM02_TALK_CALDUS
    jsr _game_room_script_entry
    lda #1                      ; GAME_USE_HANDLED
    rts

@check_hearth:
    lda use_fallback_x
    cmp #HEARTH_X
    bne @default
    lda use_fallback_y
    cmp #HEARTH_Y
    bne @default

    lda #LONG_TAPER_TYPE
    jsr _game_inventory_has_one
    cmp #0                       ; see the Caldus check's comment above
    beq @no_taper
    lda #10                     ; STORY_ROOM02_LIGHT_TAPER
    jsr _game_room_script_entry
    lda #1                      ; GAME_USE_HANDLED
    rts

@no_taper:
    lda #11                     ; STORY_ROOM02_LIGHT_TAPER_NO_ITEM
    jsr _game_room_script_entry
    lda #1                      ; GAME_USE_HANDLED
    rts

@default:
    lda #0                      ; GAME_USE_DEFAULT
    rts
