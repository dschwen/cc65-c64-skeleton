; Poll the C64 keyboard once. The custom raster IRQ does not run the KERNAL
; keyboard scanner, so the frame-driven game loop calls SCNKEY explicitly.

.setcpu "6502"

.include "platform.inc"

.export _platform_input_poll

SCNKEY = $ff9f
GETIN  = $ffe4

.segment "LOWCODE"

_platform_input_poll:
    php
    sei
    lda CPU_PORT
    pha
    and #$f8
    ora #CPU_MAP_KERNAL
    sta CPU_PORT
    jsr SCNKEY
    jsr GETIN
    tax
    pla
    sta CPU_PORT
    plp
    txa
    rts
