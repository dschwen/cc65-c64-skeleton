; Split the screen between the editor's two charset banks.
; The KERNAL IRQ entry has already saved A/X/Y before following $0314.

.export _raster_irq_install

VIC_CTRL1      = $d011
VIC_RASTER     = $d012
VIC_MEMPTR     = $d018
VIC_IRQ_STATUS = $d019
VIC_IRQ_ENABLE = $d01a
CIA1_IRQ       = $dc0d
IRQ_VECTOR     = $0314
KERNAL_IRQ_OUT = $ea81

TILE_MEMPTR    = $18       ; screen $0400, charset $2000
TEXT_MEMPTR    = $1a       ; screen $0400, charset $2800
TEXT_RASTER    = 226       ; one line before row 22's badline

.segment "CODE"

_raster_irq_install:
    sei

    ; Own the IRQ source. This intentionally stops the KERNAL jiffy clock.
    lda #$7f
    sta CIA1_IRQ
    lda CIA1_IRQ

    lda #<raster_irq
    sta IRQ_VECTOR
    lda #>raster_irq
    sta IRQ_VECTOR+1

    lda VIC_CTRL1
    and #$7f               ; compare against a raster line below 256
    sta VIC_CTRL1
    lda #TEXT_RASTER
    sta VIC_RASTER

    lda #$01
    sta VIC_IRQ_STATUS     ; clear a pending raster request
    lda VIC_IRQ_ENABLE
    ora #$01
    sta VIC_IRQ_ENABLE
    cli
    rts

raster_irq:
    lda VIC_RASTER
    beq @top_of_frame

    ; Entering through the KERNAL vector lands partway through line 226.
    ; Switch at the start of 227: after row 21's final glyph fetch and
    ; before the row 22 badline takes the CPU bus.
@wait_for_text_row:
    lda VIC_RASTER
    cmp #TEXT_RASTER+1
    bne @wait_for_text_row

    lda #TEXT_MEMPTR
    sta VIC_MEMPTR
    lda #0
    sta VIC_RASTER
    jmp @done

@top_of_frame:
    lda #TILE_MEMPTR
    sta VIC_MEMPTR
    lda #TEXT_RASTER
    sta VIC_RASTER

@done:
    lda #$01
    sta VIC_IRQ_STATUS
    jmp KERNAL_IRQ_OUT
