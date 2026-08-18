; Pack one 48-character, 4x7 text line into eight side-by-side sprites.
; The rightmost argument (text offset) arrives in A; line and room pointer are
; read from the cc65 C stack without changing it. Glyph pixels occupy bits 7-4
; in charset bank 1.

.setcpu "6502"

.include "platform.inc"

.import incsp2, incsp3
.importzp sp, ptr1, ptr2, ptr3
.export _overlay_render_line_packed
.export _overlay_render_line_text

SPRITE_DATA = $3a00
TEXT_CHARSET = $2800

text_ptr = ptr1
glyph_ptr = ptr2
destination = ptr3

.segment "BSS"
sprite_base:    .res 2
text_index:     .res 1
column:         .res 1
byte_in_sprite: .res 1
right_nibble:   .res 1
row:            .res 1
argument_bytes: .res 1
text_is_petscii: .res 1

.segment "HIGHCODE"

_overlay_render_line_packed:
    sta text_index
    ldx #3
    stx argument_bytes
    lda #0
    sta text_is_petscii
    ldy #0
    lda (sp),y                  ; line number
    pha
    iny
    lda (sp),y                  ; room pointer low
    tax
    iny
    lda (sp),y                  ; room pointer high
    tay
    txa

    ; text_ptr = room + ROOM_TEXT + text_index
    clc
    adc #<ROOM_TEXT
    sta text_ptr
    tya
    adc #>ROOM_TEXT
    sta text_ptr+1
    clc
    lda text_ptr
    adc text_index
    sta text_ptr
    bcc :+
    inc text_ptr+1
:

    jmp overlay_render_line_common

; C signature: overlay_render_line_text(const char* text, uint8_t line).
; The line arrives in A and the text pointer occupies two C stack bytes.
_overlay_render_line_text:
    ldx #2
    stx argument_bytes
    pha
    lda #1
    sta text_is_petscii
    ldy #0
    lda (sp),y
    sta text_ptr
    iny
    lda (sp),y
    sta text_ptr+1

overlay_render_line_common:
    ; sprite_base = SPRITE_DATA + line * 21
    pla
    beq @line_zero
    cmp #1
    beq @line_one
    lda #42
    bne @line_offset_ready
@line_one:
    lda #21
    bne @line_offset_ready
@line_zero:
    lda #0
@line_offset_ready:
    clc
    adc #<SPRITE_DATA
    sta sprite_base
    lda #>SPRITE_DATA
    adc #0
    sta sprite_base+1

    lda #0
    sta column
    sta byte_in_sprite
    sta right_nibble

@character:
    ldy #0
    lda (text_ptr),y
    bne :+
    jmp @done
:
    inc text_ptr
    bne :+
    inc text_ptr+1
:
    jsr map_glyph
    beq @character_drawn

    ; glyph_ptr = TEXT_CHARSET + glyph * 8
    tax
    asl
    asl
    asl
    sta glyph_ptr
    txa
    lsr
    lsr
    lsr
    lsr
    lsr
    clc
    adc #>TEXT_CHARSET
    sta glyph_ptr+1

    ; destination starts at the selected byte of this sprite's first row.
    clc
    lda sprite_base
    adc byte_in_sprite
    sta destination
    lda sprite_base+1
    adc #0
    sta destination+1
    lda #0
    sta row

@glyph_row:
    ldy row
    lda (glyph_ptr),y
    ldx right_nibble
    beq @left_glyph
    lsr
    lsr
    lsr
    lsr
    bne @merge_row
@left_glyph:
    and #$f0
@merge_row:
    ldy #0
    ora (destination),y
    sta (destination),y
    clc
    lda destination
    adc #3
    sta destination
    bcc :+
    inc destination+1
:
    inc row
    lda row
    cmp #7
    bne @glyph_row

@character_drawn:
    ; A short fixed delay makes the glyph crawl visible without retaining
    ; zero-page pointers while waiting across a raster interrupt.
    ldx #8
@delay_outer:
    ldy #0
@delay_inner:
    dey
    bne @delay_inner
    dex
    bne @delay_outer

    lda right_nibble
    eor #1
    sta right_nibble
    bne @advance_column
    inc byte_in_sprite
    lda byte_in_sprite
    cmp #3
    bne @advance_column
    lda #0
    sta byte_in_sprite
    clc
    lda sprite_base
    adc #64
    sta sprite_base
    bcc :+
    inc sprite_base+1
:
@advance_column:
    inc column
    lda column
    cmp #48
    beq @done
    jmp @character
@done:
    lda argument_bytes
    cmp #2
    beq @done_text
    jmp incsp3
@done_text:
    jmp incsp2

; A = ASCII room byte or cc65 PETSCII C-string byte, depending on the entry
; point used. Returns A = charset character index or zero for blank.
map_glyph:
    ldx text_is_petscii
    beq @ascii
    cmp #$c1                       ; cc65 literal 'A'
    bcc @petscii_lower
    cmp #$db
    bcs @unsupported
    sec
    sbc #$80                       ; PETSCII upper case -> ASCII upper case
    bne @ascii
@petscii_lower:
    cmp #$41                       ; cc65 literal 'a'
    bcc @ascii                     ; punctuation is already ASCII-compatible
    cmp #$5b
    bcs @ascii
    clc
    adc #$20                       ; PETSCII lower case -> ASCII lower case
@ascii:
    cmp #128
    bcs @unsupported
    tax
    lda ascii_glyph,x
    rts
@unsupported:
    lda #0
    rts

.segment "MIDRODATA"
ascii_glyph:
    .res 33, 0                    ; $00-$20
    .byte 253                     ; !
    .res 2, 0                     ; " #
    .byte 220                     ; $
    .res 3, 0                     ; % & '
    .byte 219, 221                ; ( )
    .res 2, 0                     ; * +
    .byte 252, 222, 251           ; , - .
    .res 11, 0                    ; / through 9
    .byte 255                     ; :
    .res 4, 0                     ; ; < = >
    .byte 254                     ; ?
    .byte 0                       ; @
    .byte 193,194,195,196,197,198,199,200,201,202,203,204,205
    .byte 206,207,208,209,210,211,212,213,214,215,216,217,218
    .res 6, 0                     ; [ through `
    .byte 225,226,227,228,229,230,231,232,233,234,235,236,237
    .byte 238,239,240,241,242,243,244,245,246,247,248,249,250
    .res 5, 0                     ; { through DEL
