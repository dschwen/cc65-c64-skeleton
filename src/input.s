; Poll the C64 keyboard once. The custom raster IRQ does not run the KERNAL
; keyboard scanner, so the frame-driven game loop calls SCNKEY explicitly.

.setcpu "6502"

.include "platform.inc"

.export _platform_input_poll
.import _raster_irq_resync

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
    ; SCNKEY/GETIN run with interrupts disabled for however long the
    ; KERNAL's keyboard scan/debounce takes, which can occasionally
    ; straddle one of the raster IRQ's two precisely-timed charset-switch
    ; points (TEXT_RASTER or line 0) and delay it. raster_irq_body's own
    ; recovery path for a late line-0 event is what actually matters here
    ; (see its comment in src/irq.s - it used to skip the
    ; platform_text_screen_active check, the real cause of the inventory/
    ; save screens' reported charset flicker); this resync is a cheap,
    ; harmless belt-and-suspenders on top of that fix, the same self-
    ; healing already used after every far-call unwind for exactly this
    ; class of hazard - it did not by itself resolve the flicker when
    ; tried in isolation (confirmed live), so don't rely on it alone if
    ; this code is ever refactored.
    jsr _raster_irq_resync
    plp
    txa
    rts
