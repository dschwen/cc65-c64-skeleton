.setcpu "6502"

.export _game_inventory_draw_item_native

.import _game_inventory_draw_index
.import _game_inventory_draw_type
.import _game_inventory_draw_quantity
.import _platform_object_type_get
.importzp ptr1

SCREEN_LEFT  = $0400 + 2 * 40 + 1
SCREEN_RIGHT = $0400 + 2 * 40 + 21
TYPE_NAME    = 2
TYPE_NAME_LENGTH = 14

.segment "UPPERCODE"

; Draw one inventory entry. The C caller supplies a compact list index, type,
; and quantity in resident globals to avoid cc65's multi-argument stack cost.
_game_inventory_draw_item_native:
    lda _game_inventory_draw_index
    cmp #16
    bcc @left_column
    sbc #16
    tax
    lda #<SCREEN_RIGHT
    sta inventory_put+1
    lda #>SCREEN_RIGHT
    bne @set_high
@left_column:
    tax
    lda #<SCREEN_LEFT
    sta inventory_put+1
    lda #>SCREEN_LEFT
@set_high:
    sta inventory_put+2

    ; Advance the destination by 40 bytes per displayed row.
    txa
    beq @write_quantity
@advance_row:
    clc
    lda inventory_put+1
    adc #40
    sta inventory_put+1
    bcc :+
    inc inventory_put+2
:
    dex
    bne @advance_row

@write_quantity:
    lda _game_inventory_draw_quantity
    ldx #0
@hundreds:
    cmp #100
    bcc @hundreds_done
    sbc #100
    inx
    bne @hundreds
@hundreds_done:
    pha
    ldy #0                    ; nonzero once a leading digit was emitted
    cpx #0
    beq @skip_hundreds
    txa
    ora #'0'
    jsr put_char
    iny
@skip_hundreds:
    pla
    ldx #0
@tens:
    cmp #10
    bcc @tens_done
    sbc #10
    inx
    bne @tens
@tens_done:
    pha
    cpx #0
    bne @write_tens
    cpy #0
    beq @skip_tens
@write_tens:
    txa
    ora #'0'
    jsr put_char
@skip_tens:
    pla
    ora #'0'
    jsr put_char
    lda #' '
    jsr put_char

    lda _game_inventory_draw_type
    jsr _platform_object_type_get
    clc
    adc #TYPE_NAME
    bcc :+
    inx
:
    sta ptr1
    stx ptr1+1
    ldy #0
@name:
    lda (ptr1),y
    beq @done
    ; Asset names are ASCII. Convert them to the screen-code convention used
    ; by platform_text_screen_code(): upper case $41-$5A, lower case $01-$1A.
    cmp #$41
    bcc @put_name
    cmp #$5b
    bcs @lower_case
    bne @put_name
@lower_case:
    cmp #$61
    bcc @put_name
    cmp #$7b
    bcs @put_name
    sec
    sbc #$60
@put_name:
    jsr put_char
    iny
    cpy #TYPE_NAME_LENGTH
    bne @name
@done:
    rts

put_char:
inventory_put:
    sta $ffff
    inc inventory_put+1
    bne :+
    inc inventory_put+2
:
    rts
