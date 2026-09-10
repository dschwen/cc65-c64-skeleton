.setcpu "6502"

.export _game_inventory_draw_item_native

.import _game_inventory_draw_index
.import _game_inventory_draw_type
.import _game_inventory_draw_quantity
.import _platform_object_type_info_get
.importzp ptr1
.importzp ptr2
.importzp regsave

SCREEN_LEFT  = $0400 + 2 * 40 + 1
SCREEN_RIGHT = $0400 + 2 * 40 + 21
TYPE_NAME_LENGTH = 14

.segment "CODE"

; Draw one compact inventory entry. Columns zero and twenty are deliberately
; left free for the selection cursor drawn by inventory.c.
_game_inventory_draw_item_native:
    lda _game_inventory_draw_index
    cmp #16
    bcc @left_column
    sbc #16
    tax
    lda #<SCREEN_RIGHT
    sta ptr2
    lda #>SCREEN_RIGHT
    bne @set_high
@left_column:
    tax
    lda #<SCREEN_LEFT
    sta ptr2
    lda #>SCREEN_LEFT
@set_high:
    sta ptr2+1

    txa
    beq @write_quantity
@advance_row:
    clc
    lda ptr2
    adc #40
    sta ptr2
    bcc :+
    inc ptr2+1
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
    ldy #0
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

    ; The type-info getter is itself a nested far call (bank 48 -> bank 47,
    ; with its data fetched from bank 46), and cc65's shared ZP scratch is
    ; caller-clobbered. Preserve our screen destination around it.
    lda ptr2
    pha
    lda ptr2+1
    pha
    lda _game_inventory_draw_type
    jsr _platform_object_type_info_get
    sta ptr1
    stx ptr1+1
    pla
    sta ptr2+1
    pla
    sta ptr2
    ldy #0
@name:
    lda (ptr1),y
    beq @done
    cmp #$41
    bcc @put_name
    cmp #$5b
    bcs @upper_case
    sec
    sbc #$40
    bne @put_name
@upper_case:
    cmp #$c1
    bcc @put_name
    cmp #$db
    bcs @put_name
    sec
    sbc #$80
@put_name:
    jsr put_char
    iny
    cpy #TYPE_NAME_LENGTH
    bne @name
@done:
    rts

put_char:
    ; In-place cartridge code is read-only: the overlay version patched the
    ; operand of an absolute STA here, but writes under ROM cannot change the
    ; bytes subsequently fetched from ROM. Use an indirect RAM pointer and
    ; preserve Y, which is the object-name source index in the caller.
    sta regsave
    sty regsave+1
    ldy #0
    lda regsave
    sta (ptr2),y
    inc ptr2
    bne :+
    inc ptr2+1
:
    ldy regsave+1
    rts
