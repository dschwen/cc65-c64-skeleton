; Hand-written enter_room for room 00: sets up rain's 7 dedicated sprites
; (pointer/color/VIC-attribute registers), then hands control to
; platform_rain_enable() with this setup routine as its callback - see
; platform_rain_enable() in src/platform.h for why sprite setup runs from
; room code rather than staying resident. Direct translation of rooms/00.c's
; former rain_setup()/enter_room() - same registers, same values, same
; order. Spliced verbatim into the DSL-generated room-00 output by
; tools/compile_room.py (enter_room: asm "rooms/asm/00_enter_room.s").
.setcpu "6502"

.import _platform_rain_enable

.export _enter_room

VIC_SPRITE_POINTERS = $07f8
VIC_SPRITE_ENABLE   = $d015
VIC_SPRITE_YEXPAND  = $d017
VIC_SPRITE_PRIORITY = $d01b
VIC_SPRITE_XEXPAND  = $d01d
VIC_SPRITE0_COLOR   = $d027
RAIN_BITMAP_POINTER = $3b80 / 64
RAIN_SPRITE_MASK     = $fe   ; sprites 1-7
RAIN_SPRITE_MASK_INV = $01   ; ~RAIN_SPRITE_MASK & $ff (sprites 1-7 span all
                              ; of bits 1-7, so the complement is bit 0 alone)

.segment "CODE"

_enter_room:
    lda #<rain_setup
    ldx #>rain_setup
    jmp _platform_rain_enable   ; tail call: rain_setup runs later, as the
                                 ; callback - platform_rain_enable is void
                                 ; and this is our own last action, so jmp
                                 ; instead of jsr+rts

rain_setup:
    ldx #1
@loop:
    lda #RAIN_BITMAP_POINTER
    sta VIC_SPRITE_POINTERS,x
    lda #6                      ; dark blue
    sta VIC_SPRITE0_COLOR,x
    inx
    cpx #8
    bne @loop

    lda VIC_SPRITE_YEXPAND
    and #RAIN_SPRITE_MASK_INV   ; Y-expand off
    sta VIC_SPRITE_YEXPAND

    lda VIC_SPRITE_PRIORITY
    ora #RAIN_SPRITE_MASK       ; priority: behind
    sta VIC_SPRITE_PRIORITY

    lda VIC_SPRITE_XEXPAND
    and #RAIN_SPRITE_MASK_INV   ; X-expand off: true 45-degree diagonal
    sta VIC_SPRITE_XEXPAND

    lda VIC_SPRITE_ENABLE
    ora #RAIN_SPRITE_MASK       ; enable
    sta VIC_SPRITE_ENABLE
    rts
