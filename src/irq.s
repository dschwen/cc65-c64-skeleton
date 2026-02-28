; irq.s - optional raster IRQ stub (not wired up by default)
; If you want to use this:
; 1) install vectors and enable raster IRQ in C
; 2) keep handler tiny; set a flag; ack $D019
;
; This file is intentionally left as a stub for expansion.

.export _irq_stub

.segment "CODE"

_irq_stub:
    rts
