; Poll the C64 keyboard once. The custom raster IRQ does not run the KERNAL
; keyboard scanner, so the frame-driven game loop calls SCNKEY explicitly.

.setcpu "6502"

.export _platform_input_poll

SCNKEY = $ff9f
GETIN  = $ffe4

.segment "HIGHCODE"

_platform_input_poll:
    jsr SCNKEY
    jmp GETIN
