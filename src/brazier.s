; game_ignite_brazier() - hand-written because the equivalent C loop (see
; src/game_support.c's comment) compiled to roughly 500 bytes, well past
; what HIGHCODE's tight budget could absorb for what is a small function.
;
; uint8_t __fastcall__ game_ignite_brazier(uint8_t tile_x, uint8_t tile_y)
;
; Called from rooms/asm/04_use_at.s: tile_x already pushed by the caller
; (offset 0 on the software stack), tile_y in A - the callee does not
; touch sp, matching cc65's own fastcall convention (the caller pops tile_x
; afterward). Scans platform_room.objects for an unlit Brazier (object type
; 41) at (tile_x, tile_y); if the player carries a Lit Taper (type 53), it
; is swapped in place for a Lit Brazier (type 40), the room is redrawn, and
; STORY_ROOM04_LIGHT_BRAZIER (key 3) runs. No taper in hand runs
; STORY_ROOM04_LIGHT_BRAZIER_NO_ITEM (key 4) instead. No unlit brazier at
; that tile returns GAME_USE_DEFAULT (0) so the caller falls through.
.setcpu "6502"

.import _platform_room
.import _platform_player
.import _platform_room_draw
.import _game_inventory_has_one
.import _game_room_script_entry
.import _game_world_capture_current
.import pushax
.import incsp2
.importzp sp
.importzp ptr1

.export _game_ignite_brazier

BRAZIER_UNLIT_TYPE = 41
BRAZIER_LIT_TYPE   = 40
LIT_TAPER_TYPE      = 53
; sizeof(PlatformRoom) header (13) + tiles (220) - see the PlatformRoom
; doc comment in src/platform.h.
OBJECTS_OFFSET = 233

.segment "BSS"
brazier_tile_x:  .res 1
brazier_tile_y:  .res 1
brazier_count:   .res 1
brazier_has_taper: .res 1

.segment "UPPERCODE"

_game_ignite_brazier:
    sta brazier_tile_y      ; last fastcall arg (tile_y) arrives in A
    ldy #0
    lda (sp),y               ; first arg (tile_x), pushed by the caller;
    sta brazier_tile_x       ; sp is left untouched - caller pops it

    ; Resolved once up front, before ptr1 is even needed below - has_one()'s
    ; result doesn't depend on which brazier (if any) matches, and this way
    ; nothing below needs to survive a call across a live ptr1 (a shared
    ; cc65 runtime scratch register).
    lda #LIT_TAPER_TYPE
    jsr _game_inventory_has_one
    sta brazier_has_taper

    ; A fixed scan bound instead of a platform_room_object_limit() call -
    ; every UPPER-area byte is spoken for (see this file's header comment);
    ; room 04 has 2 objects total today and is very unlikely to ever need
    ; more than a handful, well under this margin.
    lda #40
    sta brazier_count

    lda #<(_platform_room+OBJECTS_OFFSET)
    sta ptr1
    lda #>(_platform_room+OBJECTS_OFFSET)
    sta ptr1+1

@loop:
    lda brazier_count
    beq @miss

    ldy #0
    lda (ptr1),y              ; object->type
    cmp #BRAZIER_UNLIT_TYPE
    bne @next
    iny
    lda (ptr1),y              ; object->x
    lsr a
    cmp brazier_tile_x
    bne @next
    iny
    lda (ptr1),y              ; object->y
    lsr a
    cmp brazier_tile_y
    bne @next

    lda brazier_has_taper
    beq @no_taper

    ldy #0
    lda #BRAZIER_LIT_TYPE
    sta (ptr1),y
    jsr _game_world_capture_current
    lda #<_platform_room
    ldx #>_platform_room
    jsr pushax
    lda _platform_player
    ldx _platform_player+1
    jsr _platform_room_draw
    jsr incsp2                ; pop the &platform_room pushed above
    lda #3                    ; STORY_ROOM04_LIGHT_BRAZIER
    jmp @handled

@no_taper:
    lda #4                    ; STORY_ROOM04_LIGHT_BRAZIER_NO_ITEM

@handled:
    jsr _game_room_script_entry
    lda #1                    ; GAME_USE_HANDLED
    rts

@next:
    lda ptr1
    clc
    adc #3
    sta ptr1
    bcc @nohi
    inc ptr1+1
@nohi:
    dec brazier_count
    jmp @loop

@miss:
    lda #0                    ; GAME_USE_DEFAULT
    rts
