.setcpu "6502"

.include "platform.inc"

.export _platform_text_output_native

.import _platform_wait_frame
.import _platform_input_poll
.import _platform_text_output_color
.import _platform_text_output_line

TEXT_ROW_0  = SCREEN_RAM + 23 * MAP_WIDTH_CHARS
TEXT_ROW_1  = SCREEN_RAM + 24 * MAP_WIDTH_CHARS
COLOR_ROW_0 = COLOR_RAM  + 23 * MAP_WIDTH_CHARS
COLOR_ROW_1 = COLOR_RAM  + 24 * MAP_WIDTH_CHARS

.segment "TEXTCODE"

; fastcall: A/X points at a zero-terminated cc65/PETSCII string. Words wrap at
; 40 columns. Once both status rows are occupied, wait for a fresh keypress,
; scroll the lower row upward, and continue on the cleared lower row.
_platform_text_output_native:
    sta text_source_read+1
    stx text_source_read+2
    lda #0
    sta text_column
    jsr clear_text_rows

text_next_token:
    ldy #0
text_source_read:
    lda $ffff,y
    bne :+
    jmp text_done
:
    cmp #' '
    beq text_space
    cmp #$0a
    beq text_explicit_newline
    cmp #$0d
    beq text_explicit_newline

    ; Look ahead to the next delimiter. Forty-byte chunks also make words
    ; longer than one screen line wrap without requiring a word buffer.
    lda text_source_read+1
    sta text_measure_read+1
    lda text_source_read+2
    sta text_measure_read+2
    ldy #0
text_measure_read:
    lda $ffff,y
    beq text_word_measured
    cmp #' '
    beq text_word_measured
    cmp #$0a
    beq text_word_measured
    cmp #$0d
    beq text_word_measured
    iny
    cpy #MAP_WIDTH_CHARS
    bne text_measure_read
text_word_measured:
    sty word_length
    lda text_column
    beq text_write_word
    clc
    adc word_length
    cmp #MAP_WIDTH_CHARS+1
    bcc text_write_word
    jsr next_text_line

text_write_word:
    lda word_length
    beq text_next_token
    lda text_source_read+1
    sta text_word_read+1
    lda text_source_read+2
    sta text_word_read+2
    ldy #0
text_word_read:
    lda $ffff,y
    jsr write_character
    jsr advance_source
    dec word_length
    bne text_write_word
    jmp text_next_token

text_space:
    jsr advance_source
    lda text_column
    beq text_next_token
    cmp #MAP_WIDTH_CHARS
    bcs text_space_wrap
    lda #' '
    jsr write_character
    jmp text_next_token
text_space_wrap:
    jsr next_text_line
    jmp text_next_token

text_explicit_newline:
    jsr advance_source
    jsr next_text_line
    jmp text_next_token

text_done:
    rts

advance_source:
    inc text_source_read+1
    bne :+
    inc text_source_read+2
:
    rts

; Convert the same PETSCII ranges as platform_text_screen_code(), then write
; the character and its color to the active status row.
write_character:
    cmp #$41
    bcc text_character_ready
    cmp #$5b
    bcs text_check_uppercase
    sec
    sbc #$40
    bne text_character_ready
text_check_uppercase:
    cmp #$c1
    bcc text_character_ready
    cmp #$db
    bcs text_character_ready
    sec
    sbc #$80
text_character_ready:
    ldx text_column
    ldy _platform_text_output_line
    beq text_write_top
    sta TEXT_ROW_1,x
    lda _platform_text_output_color
    sta COLOR_ROW_1,x
    jmp text_character_done
text_write_top:
    sta TEXT_ROW_0,x
    lda _platform_text_output_color
    sta COLOR_ROW_0,x
text_character_done:
    inc text_column
    rts

next_text_line:
    lda #0
    sta text_column
    lda _platform_text_output_line
    bne text_scroll_needed
    inc _platform_text_output_line
    rts
text_scroll_needed:
    jsr wait_for_fresh_key
    ldx #0
text_scroll:
    lda TEXT_ROW_1,x
    sta TEXT_ROW_0,x
    lda COLOR_ROW_1,x
    sta COLOR_ROW_0,x
    lda #' '
    sta TEXT_ROW_1,x
    lda #0
    sta COLOR_ROW_1,x
    inx
    cpx #MAP_WIDTH_CHARS
    bne text_scroll
    rts

wait_for_fresh_key:
text_wait_release:
    jsr _platform_wait_frame
    jsr _platform_input_poll
    bne text_wait_release
text_wait_press:
    jsr _platform_wait_frame
    jsr _platform_input_poll
    beq text_wait_press
text_wait_final_release:
    jsr _platform_wait_frame
    jsr _platform_input_poll
    bne text_wait_final_release
    rts

clear_text_rows:
    ldx #MAP_WIDTH_CHARS-1
    lda #' '
text_clear_screen:
    sta TEXT_ROW_0,x
    sta TEXT_ROW_1,x
    dex
    bpl text_clear_screen
    ldx #MAP_WIDTH_CHARS-1
    lda #0
text_clear_color:
    sta COLOR_ROW_0,x
    sta COLOR_ROW_1,x
    dex
    bpl text_clear_color
    rts

; The module is copied to RAM, so its two bytes of private workspace remain
; writable without consuming resident BSS.
text_column: .byte 0
word_length: .byte 0
